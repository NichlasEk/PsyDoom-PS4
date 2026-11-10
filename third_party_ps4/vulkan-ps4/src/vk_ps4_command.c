/*
 * vk_ps4_command.c — VkCommandBuffer implementation via GnmCommandBuffer.
 *
 * vkBeginCommandBuffer allocates PM4 buffer and inits default hardware state.
 * vkCmdBindPipeline emits SetVsShader/SetPsShader + state registers.
 * vkCmdSetViewport / SetScissor emit corresponding PM4 packets.
 * vkCmdDraw emits DrawIndexAuto.
 * vkCmdBeginRenderPass emits SetRenderTarget + clear.
 * vkCmdEndRenderPass emits EOP event.
 * vkEndCommandBuffer finalizes the PM4 stream.
 */

#include "vk_ps4_internal.h"
#include "gpuaddr.h"

#include <string.h>
#include <math.h>

/* PM4 register definitions for direct register programming
 * (stencil ref/mask/ops don't have GNM API wrappers). */
#include <pm4/sid.h>
#include <pm4/amdgfxregs.h>

#include "vk_ps4_pm4_chunks.h"
#define VK_PS4_CMD_BUFFER_SIZE VK_PS4_PM4_RECORDING_BYTES

/* Record API boundaries without moving PM4 or its embedded data. */
static void vk_ps4_mark_operation(VkPs4CommandBuffer *cmd, const char *name) {
    if (!cmd || !vk_ps4_operation_diagnostic()) return;
    uint32_t offset = (uint32_t)(cmd->gnm_cmd.cmdptr - cmd->gnm_cmd.beginptr);
    uint32_t n = cmd->diagnostic_count;
    if (n && cmd->diagnostic_offsets[n-1] == offset) {
        cmd->diagnostic_names[n-1] = name;
        return;
    }
    if (n == 256) { cmd->resource_error = VK_ERROR_OUT_OF_HOST_MEMORY; return; }
    cmd->diagnostic_offsets[n] = offset;
    cmd->diagnostic_names[n] = name;
    cmd->diagnostic_count++;
}

/* Forward declaration from vk_ps4_pipeline.c */
extern GnmPrimitiveType vk_topology_to_gnm(VkPrimitiveTopology topology);

/* Forward declarations for format helpers defined later in this file */
static uint32_t vk_format_to_bpp(VkFormat fmt);

/* Check if a VkFormat has a depth component. */
static bool vk_format_has_depth(VkFormat fmt) {
    switch (fmt) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
        return true;
    default:
        return false;
    }
}

/* Check if a VkFormat has a stencil component. */
static bool vk_format_has_stencil(VkFormat fmt) {
    switch (fmt) {
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
    case VK_FORMAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

/* === PM4 helpers for direct context register emission === */

/* Emit a single PM4 SET_CONTEXT_REG packet.
 * Used for registers that don't have GNM API wrappers
 * (stencil ref/mask, stencil ops, stencil clear, depth bounds). */
static void vk_ps4_emit_context_reg(GnmCommandBuffer *cmd, uint32_t reg_addr, uint32_t value) {
    /* Validate register address is in context register space */
    if (reg_addr < SI_CONTEXT_REG_OFFSET || reg_addr >= SI_CONTEXT_REG_END) {
        return;  /* Invalid register address — silently skip */
    }

    const uint32_t num_dwords = 3;  /* header + reg offset + value */

    /* Try to resize if not enough space (like GNM's setcontextregister does) */
    if ((uint32_t)(cmd->endptr - cmd->cmdptr) < num_dwords) {
        if (cmd->callback.func) {
            cmd->callback.func(cmd, num_dwords, cmd->callback.userdata);
        }
        if ((uint32_t)(cmd->endptr - cmd->cmdptr) < num_dwords) {
            return;  /* Still not enough space — silently skip */
        }
    }

    cmd->cmdptr[0] = PKT3(PKT3_SET_CONTEXT_REG, 1, 0);
    cmd->cmdptr[1] = (reg_addr - SI_CONTEXT_REG_OFFSET) >> 2;
    cmd->cmdptr[2] = value;
    cmd->cmdptr += num_dwords;
}

/* === Vulkan-to-GNM enum mappings for depth/stencil === */

/* VkStencilOp → PM4 V_02842C_STENCIL_* values. */
uint32_t vk_stencil_op_to_pm4(VkStencilOp op) {
    switch (op) {
    case VK_STENCIL_OP_KEEP:                return V_02842C_STENCIL_KEEP;       /* 0 */
    case VK_STENCIL_OP_ZERO:                return V_02842C_STENCIL_ZERO;       /* 1 */
    case VK_STENCIL_OP_REPLACE:             return V_02842C_STENCIL_REPLACE_TEST;/* 3 */
    case VK_STENCIL_OP_INCREMENT_AND_CLAMP: return V_02842C_STENCIL_ADD_CLAMP;  /* 5 */
    case VK_STENCIL_OP_DECREMENT_AND_CLAMP: return V_02842C_STENCIL_SUB_CLAMP;  /* 6 */
    case VK_STENCIL_OP_INVERT:              return V_02842C_STENCIL_INVERT;     /* 7 */
    case VK_STENCIL_OP_INCREMENT_AND_WRAP:  return V_02842C_STENCIL_ADD_WRAP;   /* 8 */
    case VK_STENCIL_OP_DECREMENT_AND_WRAP:  return V_02842C_STENCIL_SUB_WRAP;   /* 9 */
    default:                                return V_02842C_STENCIL_KEEP;
    }
}

/* Convert float to uint32 bit pattern (like fui()). */
static inline uint32_t vk_ps4_fui(float f) {
    union { float f; uint32_t u; } v;
    v.f = f;
    return v.u;
}

/* Convert float32 to float16 (IEEE 754 half precision).
 * Implements the standard round-to-nearest-even conversion. */
static inline uint16_t vk_ps4_float_to_half(float f) {
    union { float f; uint32_t u; } v;
    v.f = f;
    uint32_t x = v.u;
    uint32_t sign = (x >> 31) & 1;
    uint32_t exp = (x >> 23) & 0xFF;
    uint32_t mant = x & 0x7FFFFF;

    if (exp == 0xFF) {
        /* Inf or NaN → half Inf/NaN */
        if (mant == 0) {
            return (uint16_t)((sign << 15) | 0x7C00);
        } else {
            /* NaN: preserve mantissa bits, set at least one */
            return (uint16_t)((sign << 15) | 0x7C00 | (mant >> 13) | 1);
        }
    }

    /* Bias adjust: 127 → 15 */
    int32_t new_exp = (int32_t)exp - 127 + 15;

    if (new_exp <= 0) {
        /* Underflow to denormal or zero */
        if (new_exp < -10) {
            /* Too small → zero */
            return (uint16_t)(sign << 15);
        }
        /* Denormal: shift mantissa by (14 - new_exp) bits with rounding */
        mant = mant | 0x800000;  /* implicit 1 */
        uint32_t shift = (uint32_t)(14 - new_exp);
        uint32_t result_mant = mant >> shift;
        /* Round-to-nearest-even */
        uint32_t round_bit = (mant >> (shift - 1)) & 1;
        uint32_t sticky = (shift > 1) ? ((mant & ((1U << (shift - 1)) - 1)) != 0) : 0;
        if (round_bit && (sticky || (result_mant & 1))) {
            result_mant++;
        }
        return (uint16_t)((sign << 15) | result_mant);
    }

    if (new_exp >= 0x1F) {
        /* Overflow → half Inf */
        return (uint16_t)((sign << 15) | 0x7C00);
    }

    /* Normal: pack sign, exponent, mantissa with rounding */
    uint32_t result = (sign << 15) | ((uint32_t)new_exp << 10) | (mant >> 13);
    /* Round-to-nearest-even based on truncated bits */
    uint32_t round_bit = (mant >> 12) & 1;
    uint32_t sticky = (mant & 0xFFF) != 0;
    if (round_bit && (sticky || (result & 1))) {
        result++;
    }
    return (uint16_t)result;
}

/* Pack a VkClearColorValue into a 32-bit FillMemory value based on the format.
 * FillMemory writes 32-bit values to 4-byte-aligned addresses, so for
 * formats smaller than 32 bpp, the clear value is replicated to fill
 * the full 32-bit word. */
static uint32_t vk_ps4_pack_clear_val_32(VkFormat fmt, const VkClearColorValue *cc) {
    uint32_t bpp = vk_format_to_bpp(fmt);

    switch (fmt) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32: {
        /* R8G8B8A8 memory layout: R,G,B,A bytes.
         * Pack float [0,1] → uint8 for UNORM formats. */
        uint8_t r = (uint8_t)(cc->float32[0] * 255.0f + 0.5f);
        uint8_t g = (uint8_t)(cc->float32[1] * 255.0f + 0.5f);
        uint8_t b = (uint8_t)(cc->float32[2] * 255.0f + 0.5f);
        uint8_t a = (uint8_t)(cc->float32[3] * 255.0f + 0.5f);
        return (uint32_t)r | ((uint32_t)g << 8) |
               ((uint32_t)b << 16) | ((uint32_t)a << 24);
    }
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB: {
        /* B8G8R8A8 memory layout: B,G,R,A bytes — swap R and B. */
        uint8_t r = (uint8_t)(cc->float32[0] * 255.0f + 0.5f);
        uint8_t g = (uint8_t)(cc->float32[1] * 255.0f + 0.5f);
        uint8_t b = (uint8_t)(cc->float32[2] * 255.0f + 0.5f);
        uint8_t a = (uint8_t)(cc->float32[3] * 255.0f + 0.5f);
        return (uint32_t)b | ((uint32_t)g << 8) |
               ((uint32_t)r << 16) | ((uint32_t)a << 24);
    }
    case VK_FORMAT_R8G8B8A8_UINT:
    case VK_FORMAT_R8G8B8A8_SINT:
    case VK_FORMAT_R8G8B8A8_SNORM: {
        /* Integer formats: use uint32 components directly */
        uint8_t r = (uint8_t)(cc->uint32[0] & 0xFF);
        uint8_t g = (uint8_t)(cc->uint32[1] & 0xFF);
        uint8_t b = (uint8_t)(cc->uint32[2] & 0xFF);
        uint8_t a = (uint8_t)(cc->uint32[3] & 0xFF);
        return (uint32_t)r | ((uint32_t)g << 8) |
               ((uint32_t)b << 16) | ((uint32_t)a << 24);
    }
    case VK_FORMAT_R8_UNORM:
    case VK_FORMAT_R8_SRGB: {
        /* 8-bit: replicate 4 times */
        uint8_t v = (uint8_t)(cc->float32[0] * 255.0f + 0.5f);
        return (uint32_t)v | ((uint32_t)v << 8) |
               ((uint32_t)v << 16) | ((uint32_t)v << 24);
    }
    case VK_FORMAT_R16_SFLOAT: {
        /* 16-bit float: convert float32 → float16 (IEEE 754 half). */
        uint16_t h = vk_ps4_float_to_half(cc->float32[0]);
        return (uint32_t)h | ((uint32_t)h << 16);
    }
    case VK_FORMAT_R16_UNORM: {
        /* 16-bit UNORM: convert float [0,1] → uint16 [0,65535]. */
        uint16_t h = (uint16_t)(cc->float32[0] * 65535.0f + 0.5f);
        return (uint32_t)h | ((uint32_t)h << 16);
    }
    case VK_FORMAT_R16G16_SFLOAT: {
        /* 32-bit = two float16 values */
        uint16_t r = vk_ps4_float_to_half(cc->float32[0]);
        uint16_t g = vk_ps4_float_to_half(cc->float32[1]);
        return (uint32_t)r | ((uint32_t)g << 16);
    }
    case VK_FORMAT_R16G16_UNORM: {
        /* 32-bit = two uint16 UNORM values */
        uint16_t r = (uint16_t)(cc->float32[0] * 65535.0f + 0.5f);
        uint16_t g = (uint16_t)(cc->float32[1] * 65535.0f + 0.5f);
        return (uint32_t)r | ((uint32_t)g << 16);
    }
    case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16_SINT: {
        /* 32-bit = two 16-bit integer values */
        uint16_t r = (uint16_t)cc->uint32[0];
        uint16_t g = (uint16_t)cc->uint32[1];
        return (uint32_t)r | ((uint32_t)g << 16);
    }
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_D32_SFLOAT:
        return cc->uint32[0];
    default:
        /* For 32-bit formats, use first float component bits */
        if (bpp == 4) return vk_ps4_fui(cc->float32[0]);
        /* For other sizes, replicate the first byte */
        if (bpp == 1) {
            uint8_t v = (uint8_t)vk_ps4_fui(cc->float32[0]);
            return (uint32_t)v | ((uint32_t)v << 8) |
                   ((uint32_t)v << 16) | ((uint32_t)v << 24);
        }
        if (bpp == 2) {
            uint16_t v = (uint16_t)vk_ps4_fui(cc->float32[0]);
            return (uint32_t)v | ((uint32_t)v << 16);
        }
        return vk_ps4_fui(cc->float32[0]);
    }
}

/* Fill a linear region of memory with a 32-bit value (row-by-row for sub-rect).
 * NOTE: FillMemory writes 32-bit values to 4-byte-aligned addresses.
 * For tiled RTs, this produces incorrect results because the memory layout
 * is swizzled. A shader-based clear is needed for tiled RTs (future work). */
static void vk_ps4_fill_rect_linear(GnmCommandBuffer *cmd,
                                     uint64_t base_addr, uint32_t bpp,
                                     uint32_t rect_x, uint32_t rect_y,
                                     uint32_t rect_w, uint32_t rect_h,
                                     uint32_t surface_w, uint32_t surface_h,
                                     uint32_t clear_val) {
    /* Fill row-by-row to handle sub-rect clears.
     * Round up row_size to 4-byte multiple for FillMemory alignment. */
    for (uint32_t y = 0; y < rect_h && (rect_y + y) < surface_h; y++) {
        uint64_t row_addr = base_addr +
                            (uint64_t)(rect_y + y) * surface_w * bpp +
                            (uint64_t)rect_x * bpp;
        uint32_t row_size = rect_w * bpp;
        if (rect_x + rect_w > surface_w)
            row_size = (surface_w - rect_x) * bpp;
        /* FillMemory requires 4-byte alignment and size multiple of 4 */
        if (row_addr & 3) continue;
        row_size = (row_size + 3) & ~3u;  /* round up to 4-byte multiple */
        sceGnmDrawCmdFillMemory(cmd, row_addr, row_size, clear_val);
    }
}

/* Check if a GnmRenderTarget uses tiled memory.
 * Only GNM_TM_DISPLAY_LINEAR_ALIGNED (0x8) and
 * GNM_TM_DISPLAY_LINEAR_GENERAL (0x1f) are linear; all other tile modes
 * use some form of tiling. */
static bool vk_ps4_rt_is_tiled(const GnmRenderTarget *rt) {
    GnmTileMode tm = (GnmTileMode)rt->attrib.tilemode_index;
    return tm != GNM_TM_DISPLAY_LINEAR_ALIGNED &&
           tm != GNM_TM_DISPLAY_LINEAR_GENERAL;
}

/* Draw-based color clear for tiled RTs.
 *
 * The GNM_EMBEDDED_PSH_DUMMY shader has cbshadermask=0, meaning it writes
 * NO color output. Using it for a draw-based clear would be a no-op.
 * A proper draw-based clear requires a custom pixel shader that exports
 * the clear color — this is implemented via vk_ps4_clear_color_draw which
 * uses the embedded clear PS binary (clear_ps_binary.h, compiled from
 * clear.frag). The clear PS reads vec4 color from a UBO at PS user data
 * register 0 and outputs to MRT0.
 *
 * Linear RTs use FillMemory (fast, direct memory write).
 * Tiled RTs use the draw-based clear (vk_ps4_clear_color_draw) which
 * goes through the CB hardware and respects the tiled memory layout.
 * Partial tiled RT ranges (sub-rect or multi-mip) still fall back to
 * FillMemory as a known approximation — full per-mip draw-based clears
 * are future work. */

/* FillMemory-based color clear for a render target.
 * Works correctly for linear RTs. For tiled RTs, the data written
 * will not match the tiled layout (known limitation). */
static void vk_ps4_clear_color_fillmem(GnmCommandBuffer *cmd,
                                        VkPs4Image *img,
                                        const VkClearColorValue *cc) {
    /* Swapchain storage is owned by VideoOut, not a VkDeviceMemory.
     * Its image wrappers intentionally have memory == NULL. */
    uint64_t rt_addr;
    if (img->is_swapchain_image && img->swapchain_address) {
        rt_addr = (uint64_t)img->swapchain_address;
    } else {
        if (!img->memory || !img->memory->gnm_mem.mapped) return;
        rt_addr = (uint64_t)img->memory->gnm_mem.mapped + img->memory_offset;
    }
    uint32_t bpp = vk_format_to_bpp(img->create_info.format);
    uint32_t surface_w = img->create_info.extent.width;
    uint32_t surface_h = img->create_info.extent.height;
    uint32_t clear_val = vk_ps4_pack_clear_val_32(
        img->create_info.format, cc);
    uint32_t rt_size = surface_w * surface_h * bpp;
    rt_size = (rt_size + 3) & ~3u;
    sceGnmDrawCmdFillMemory(cmd, rt_addr, rt_size, clear_val);
}

#include "../shaders/meta_clear_spirv.h"
/* Color-specialized shaders use the same ordinary pipeline path as the
 * hardware-verified probe. Entries stay alive until device destruction: recorded
 * commands may refer to shader code even when they have not been submitted yet. */
typedef struct VkPs4MetaClear {
    struct VkPs4MetaClear *next;
    uint32_t color[4];
    VkFormat format;
    VkPipeline pipeline;
    VkPipelineLayout layout;
} VkPs4MetaClear;

void vk_ps4_finish_meta_clear(VkPs4Device *dev) {
    VkPs4MetaClear *entry = dev->meta_clear_cache;
    while (entry) {
        VkPs4MetaClear *next = entry->next;
        vk_ps4_DestroyPipeline((VkDevice)dev, entry->pipeline, NULL);
        vk_ps4_DestroyPipelineLayout((VkDevice)dev, entry->layout, NULL);
        vk_ps4_free(&dev->allocator, entry);
        entry = next;
    }
    dev->meta_clear_cache = NULL;
    dev->meta_clear_count = 0;
}

static VkResult vk_ps4_get_meta_clear_unlocked(VkPs4CommandBuffer *cmd,
        VkPs4Image *img, const VkClearColorValue *color, VkPipeline *out) {
    VkPs4Device *dev=cmd->device;
    for (VkPs4MetaClear *e=dev->meta_clear_cache; e; e=e->next)
        if (e->format==img->create_info.format && !memcmp(e->color,color->float32,16)) {
            *out=e->pipeline; return VK_SUCCESS;
        }
    /* Bound the diagnostic candidate's shader memory; never evict live code. */
    if (dev->meta_clear_count >= 128) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    VkPs4MetaClear *e=vk_ps4_alloc_zero(&dev->allocator,sizeof(*e),16);
    if (!e) return VK_ERROR_OUT_OF_HOST_MEMORY;
    VkDevice device=(VkDevice)dev;
    VkShaderModule modules[2]={0};
    uint32_t fragment[sizeof(meta_clear_frag)/4]; memcpy(fragment,meta_clear_frag,sizeof(fragment));
    for (unsigned i=0;i<4;++i) memcpy(&fragment[meta_clear_color_offsets[i]],&color->float32[i],4);
    VkShaderModuleCreateInfo sm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=sizeof(meta_clear_vert),.pCode=meta_clear_vert};
    VkResult result=vk_ps4_CreateShaderModule(device,&sm,NULL,&modules[0]);
    if (result!=VK_SUCCESS) goto done;
    sm.codeSize=sizeof(fragment); sm.pCode=fragment;
    result=vk_ps4_CreateShaderModule(device,&sm,NULL,&modules[1]);
    if (result!=VK_SUCCESS) goto done;
    VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    result=vk_ps4_CreatePipelineLayout(device,&li,NULL,&e->layout);
    if (result!=VK_SUCCESS) goto done;
    VkPipelineShaderStageCreateInfo stages[2]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=modules[0],.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=modules[1],.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
    VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ba={.colorWriteMask=15};
    VkPipelineColorBlendStateCreateInfo cb={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&ba};
    VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=states};
    VkGraphicsPipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,
        .pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,.pRasterizationState=&rs,
        .pMultisampleState=&ms,.pColorBlendState=&cb,.pDynamicState=&dy,.layout=e->layout,
        .renderPass=(VkRenderPass)cmd->current_render_pass.pass};
    result=vk_ps4_CreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pi,NULL,&e->pipeline);
    if (result==VK_SUCCESS) {
        memcpy(e->color,color->float32,16); e->format=img->create_info.format;
        e->next=dev->meta_clear_cache; dev->meta_clear_cache=e; ++dev->meta_clear_count;
        *out=e->pipeline;
        vk_ps4_log("META CLEAR 1.24 pipeline cached count=%u rgba=%08x/%08x/%08x/%08x",dev->meta_clear_count,e->color[0],e->color[1],e->color[2],e->color[3]);
    }
done:
    for(unsigned i=0;i<2;++i) if(modules[i]) vk_ps4_DestroyShaderModule(device,modules[i],NULL);
    if(result!=VK_SUCCESS) {
        if(e->pipeline) vk_ps4_DestroyPipeline(device,e->pipeline,NULL);
        if(e->layout) vk_ps4_DestroyPipelineLayout(device,e->layout,NULL);
        vk_ps4_free(&dev->allocator,e);
    }
    return result;
}

static void vk_ps4_meta_clear_rect(VkPs4CommandBuffer *cmd, VkPs4Image *img,
        const VkClearColorValue *color, const VkRect2D *rect) {
    if (!rect->extent.width || !rect->extent.height) return;
    VkPipeline pipeline=VK_NULL_HANDLE;
    while (__atomic_exchange_n(&cmd->device->meta_clear_lock,1,__ATOMIC_ACQUIRE))
        __asm__ volatile("pause");
    VkResult result=vk_ps4_get_meta_clear_unlocked(cmd,img,color,&pipeline);
    __atomic_store_n(&cmd->device->meta_clear_lock,0,__ATOMIC_RELEASE);
    if(result!=VK_SUCCESS) {cmd->resource_error=result; return;}
    VkPs4Pipeline *saved=cmd->current_pipeline;
    VkPs4DescriptorSet *set=cmd->graphics_set0;
    VkViewport vp_saved=cmd->viewport0; bool vp_valid=cmd->viewport0_valid;
    VkRect2D sc_saved=cmd->screen_scissor; bool sc_valid=cmd->screen_scissor_valid;
    uint32_t front=cmd->stencil_refmask_front,back=cmd->stencil_refmask_back;
    bool stencil_valid=cmd->stencil_shadow_valid;
    VkCommandBuffer handle=(VkCommandBuffer)cmd;
    cmd->graphics_set0=NULL;
    vk_ps4_CmdBindPipeline(handle,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    sceGnmDrawCmdSetRenderTarget(&cmd->gnm_cmd,0,&img->gnm_rt);
    VkViewport vp={0,0,img->create_info.extent.width,img->create_info.extent.height,0,1};
    vk_ps4_CmdSetViewport(handle,0,1,&vp);
    vk_ps4_CmdSetScissor(handle,0,1,rect);
    vk_ps4_CmdDraw(handle,3,1,0,0);
    cmd->graphics_set0=set;
    cmd->current_pipeline=saved;
    if(saved) vk_ps4_CmdBindPipeline(handle,saved->bind_point,(VkPipeline)saved);
    if(vp_valid) vk_ps4_CmdSetViewport(handle,0,1,&vp_saved);
    if(sc_valid) vk_ps4_CmdSetScissor(handle,0,1,&sc_saved);
    cmd->viewport0=vp_saved;cmd->viewport0_valid=vp_valid;
    cmd->screen_scissor=sc_saved;cmd->screen_scissor_valid=sc_valid;
    cmd->stencil_refmask_front=front;cmd->stencil_refmask_back=back;cmd->stencil_shadow_valid=stencil_valid;
    if(stencil_valid) {
        vk_ps4_emit_context_reg(&cmd->gnm_cmd,R_028430_DB_STENCILREFMASK,front);
        vk_ps4_emit_context_reg(&cmd->gnm_cmd,R_028434_DB_STENCILREFMASK_BF,back);
    }
    if(saved && saved->bind_point==VK_PIPELINE_BIND_POINT_GRAPHICS) {
        for(unsigned stage=0;stage<2;++stage)
            for(unsigned word=0;word<32;++word)
                if(cmd->graphics_push_valid[stage] & (1u<<word))
                    vk_ps4_CmdPushConstants(handle,VK_NULL_HANDLE,
                        stage ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT,
                        word*4,4,&cmd->graphics_push_values[stage][word]);
    }
    /* Ordinary draws re-emit descriptor/vertex tables and base-instance regs.
     * The meta shaders have no PS user data or application push constants. */
}

static void vk_ps4_clear_color_draw(VkPs4CommandBuffer *cmd, VkPs4Image *img,
        const VkClearColorValue *color) {
    VkRect2D rect={{0,0},{img->create_info.extent.width,img->create_info.extent.height}};
    vk_ps4_meta_clear_rect(cmd,img,color,&rect);
}

/* Choose the appropriate color clear method based on the RT's tile mode.
 * Linear RTs can use FillMemory (fast, direct memory write).
 * Tiled RTs require a draw-based clear via the embedded clear PS. */
static void vk_ps4_clear_color(VkPs4CommandBuffer *cmd,
                                VkPs4Image *img,
                                const VkClearColorValue *cc) {
    if (img->is_render_target) {
        /* Check if the RT is tiled by looking at the tile mode in the
         * GnmRenderTarget descriptor.  Linear RTs (DISPLAY_LINEAR_GENERAL
         * or THIN_LINEAR) can use FillMemory; everything else needs the
         * draw-based clear. */
        GnmTileMode tm = (GnmTileMode)img->gnm_rt.attrib.tilemode_index;
        if (tm == GNM_TM_DISPLAY_LINEAR_GENERAL ||
            tm == GNM_TM_DISPLAY_LINEAR_ALIGNED) {
            vk_ps4_clear_color_fillmem(&cmd->gnm_cmd, img, cc);
        } else {
            vk_ps4_clear_color_draw(cmd, img, cc);
        }
    } else {
        /* Non-RT image — use FillMemory */
        vk_ps4_clear_color_fillmem(&cmd->gnm_cmd, img, cc);
    }
}

/* Draw-based depth/stencil clear using embedded fullscreen VS + dummy PS.
 * Triggers the GCN lazy clear by doing a draw that accesses depth.
 * Temporarily enables depth write to guarantee the lazy clear fires,
 * even if the currently bound pipeline has depth disabled. */
static void vk_ps4_clear_depth_draw(GnmCommandBuffer *cmd, bool write_depth) {
    /* Temporarily enable depth write so the draw accesses the depth buffer.
     * This guarantees the lazy clear fires regardless of the bound pipeline's
     * depth state. DB_DEPTH_CONTROL is restored by the next CmdBindPipeline. */
    GnmDepthStencilControl ds_write;
    memset(&ds_write, 0, sizeof(ds_write));
    ds_write.depthenable = 1;
    ds_write.zwrite = write_depth;
    ds_write.zfunc = GNM_DEPTH_COMPARE_ALWAYS;
    ds_write.stencilenable = 0;
    ds_write.depthboundsenable = 0;
    sceGnmDrawCmdSetDepthStencilControl(cmd, &ds_write);

    /* Use embedded fullscreen VS + dummy PS */
    sceGnmDrawCmdSetEmbeddedVsShader(cmd, GNM_EMBEDDED_VSH_FULLSCREEN, 0);
    sceGnmDrawCmdSetEmbeddedPsShader(cmd, GNM_EMBEDDED_PSH_DUMMY);

    /* Cover both halves using all four embedded VS corners.
     * Reset instance count to 1 — VGT_INSTANCE_COUNT is sticky and may
     * have been set to a large value by a previous draw. */
    sceGnmDrawCmdSetPrimitiveType(cmd, GNM_PT_TRISTRIP);
    sceGnmDrawCmdSetNumInstances(cmd, 1);
    sceGnmDrawCmdDrawIndexAuto(cmd, 4);
}

/* Bind render targets for the current subpass.
 * Uses the subpass description's pColorAttachments to map framebuffer
 * attachment indices to RT slots, and pDepthStencilAttachment for depth. */
/* Get the effective attachment view for a given index.
 * For imageless framebuffers, uses the views from VkRenderPassAttachmentBeginInfo
 * stored in cmd->current_render_pass.imageless_attachments.
 * For regular framebuffers, uses fb->attachments. */
static VkPs4ImageView *vk_ps4_get_attachment_view(VkPs4CommandBuffer *cmd, uint32_t att_idx) {
    VkPs4Framebuffer *fb = cmd->current_render_pass.framebuffer;
    if (!fb || att_idx >= fb->attachment_count) return NULL;
    if (fb->imageless) {
        if (att_idx >= cmd->current_render_pass.imageless_attachment_count)
            return NULL;
        return cmd->current_render_pass.imageless_attachments[att_idx];
    }
    return fb->attachments[att_idx];
}

static void vk_ps4_bind_subpass_targets(VkPs4CommandBuffer *cmd) {
    VkPs4RenderPass *rp = cmd->current_render_pass.pass;
    VkPs4Framebuffer *fb = cmd->current_render_pass.framebuffer;
    uint32_t subpass_idx = cmd->current_render_pass.current_subpass;

    if (!rp || !fb || subpass_idx >= rp->subpass_count) return;

    const VkSubpassDescription *subpass = &rp->subpasses[subpass_idx];

    /* Bind color attachments to their RT slots */
    if (subpass->pColorAttachments) {
        for (uint32_t j = 0; j < subpass->colorAttachmentCount && j < 8; j++) {
            uint32_t att_idx = subpass->pColorAttachments[j].attachment;
            if (att_idx == VK_ATTACHMENT_UNUSED) continue;

            VkPs4ImageView *view = vk_ps4_get_attachment_view(cmd, att_idx);
            if (!view || !view->image) continue;

            if (view->image->is_render_target) {
                sceGnmDrawCmdSetRenderTarget(&cmd->gnm_cmd, j, &view->image->gnm_rt);
            }
        }
    }

    /* Bind depth/stencil attachment */
    if (subpass->pDepthStencilAttachment) {
        uint32_t att_idx = subpass->pDepthStencilAttachment->attachment;
        if (att_idx != VK_ATTACHMENT_UNUSED) {
            VkPs4ImageView *view = vk_ps4_get_attachment_view(cmd, att_idx);
            if (view && view->image && view->image->is_depth_target) {
                sceGnmDrawCmdSetDepthRenderTarget(&cmd->gnm_cmd, &view->image->gnm_drt);
            }
        }
    }
}

/* Look up the framebuffer attachment index for a given color RT slot
 * in the current subpass. Returns VK_ATTACHMENT_UNUSED if not found. */
static uint32_t vk_ps4_subpass_color_attachment(VkPs4CommandBuffer *cmd, uint32_t rt_slot) {
    VkPs4RenderPass *rp = cmd->current_render_pass.pass;
    uint32_t subpass_idx = cmd->current_render_pass.current_subpass;

    if (!rp || subpass_idx >= rp->subpass_count) return VK_ATTACHMENT_UNUSED;
    if (!rp->subpasses[subpass_idx].pColorAttachments) return VK_ATTACHMENT_UNUSED;
    if (rt_slot >= rp->subpasses[subpass_idx].colorAttachmentCount) return VK_ATTACHMENT_UNUSED;

    return rp->subpasses[subpass_idx].pColorAttachments[rt_slot].attachment;
}

/* Look up the framebuffer attachment index for the depth/stencil attachment
 * in the current subpass. Returns VK_ATTACHMENT_UNUSED if not found. */
static uint32_t vk_ps4_subpass_depth_attachment(VkPs4CommandBuffer *cmd) {
    VkPs4RenderPass *rp = cmd->current_render_pass.pass;
    uint32_t subpass_idx = cmd->current_render_pass.current_subpass;

    if (!rp || subpass_idx >= rp->subpass_count) return VK_ATTACHMENT_UNUSED;
    if (!rp->subpasses[subpass_idx].pDepthStencilAttachment) return VK_ATTACHMENT_UNUSED;

    return rp->subpasses[subpass_idx].pDepthStencilAttachment->attachment;
}

/* Re-emit the current pipeline's graphics state after it has been clobbered
 * by a draw-based clear. This restores VS/PS shaders, blend state, RT mask,
 * depth/stencil control, and primitive type so the app can continue drawing
 * without re-binding its pipeline (per Vulkan spec for CmdClearAttachments). */
static void vk_ps4_rebind_pipeline_state(VkPs4CommandBuffer *cmd) {
    VkPs4Pipeline *pipe = cmd->current_pipeline;
    if (!pipe || pipe->bind_point != VK_PIPELINE_BIND_POINT_GRAPHICS) return;

    /* Re-emit primitive type */
    sceGnmDrawCmdSetPrimitiveType(&cmd->gnm_cmd,
        vk_topology_to_gnm(pipe->input_assembly_state.topology));

    /* Re-emit VS (or LS/ES for tess/GS paths) */
    if (pipe->has_ls) {
        sceGnmDrawCmdSetLsShader(&cmd->gnm_cmd, &pipe->ls_regs, 0);
        if (pipe->has_ds_vs) {
            sceGnmDrawCmdSetVsShader(&cmd->gnm_cmd, &pipe->vs_regs, 0);
        }
    } else if (pipe->has_es && pipe->has_gs) {
        /* GS-only: ES is set in the has_gs block below */
    } else if (pipe->has_es && !pipe->has_gs) {
        sceGnmDrawCmdSetEsShader(&cmd->gnm_cmd, &pipe->es_regs, 0);
    } else {
        sceGnmDrawCmdSetVsShader(&cmd->gnm_cmd, &pipe->vs_regs, 0);
    }

    /* Re-emit HS if present */
    if (pipe->has_hs) {
        uint32_t cp = pipe->tess_patch_control_points;
        if (cp > 32) cp = 32;
        if (cp == 0) cp = 3;
        uint32_t lshsconfig = S_028B58_HS_NUM_INPUT_CP(cp) |
                               S_028B58_HS_NUM_OUTPUT_CP(cp);
        sceGnmDrawCmdSetHsShader(&cmd->gnm_cmd, &pipe->hs_regs, lshsconfig);
    }

    /* Re-emit GS if present */
    if (pipe->has_gs) {
        sceGnmDrawCmdSetGsShader(&cmd->gnm_cmd, &pipe->gs_regs);
        if (pipe->has_es) {
            sceGnmDrawCmdSetEsShader(&cmd->gnm_cmd, &pipe->es_regs, 0);
        }
    }

    /* Re-emit PS if present */
    if (pipe->has_ps) {
        sceGnmDrawCmdSetPsShader(&cmd->gnm_cmd, &pipe->ps_regs);
        sceGnmDrawCmdSetPsInputUsage(&cmd->gnm_cmd, pipe->vs_exports,
            pipe->vs_export_count, pipe->ps_inputs, pipe->ps_input_count);
    }

    /* Re-emit fetch shader */
    if (pipe->has_fetch_shader && pipe->has_fetch_shader_slot && pipe->fetch_shader) {
        sceGnmDrawCmdSetPointerUserData(
            &cmd->gnm_cmd, GNM_STAGE_VS, pipe->fetch_shader_slot,
            pipe->fetch_shader
        );
    }

    /* Re-emit blend state + RT mask */
    if (pipe->has_blend_state) {
        sceGnmDrawCmdSetBlendColor(&cmd->gnm_cmd,
            pipe->blend_constants[0], pipe->blend_constants[1],
            pipe->blend_constants[2], pipe->blend_constants[3]);
        for (uint32_t j = 0; j < pipe->blend_control_count && j < 8; j++) {
            sceGnmDrawCmdSetBlendControl(&cmd->gnm_cmd, j,
                &pipe->blend_controls[j]);
        }
        sceGnmDrawCmdSetRenderTargetMask(&cmd->gnm_cmd,
            pipe->color_write_mask);
    } else {
        GnmBlendControl no_blend;
        memset(&no_blend, 0, sizeof(no_blend));
        for (uint32_t j = 0; j < 8; j++) {
            sceGnmDrawCmdSetBlendControl(&cmd->gnm_cmd, j, &no_blend);
        }
        sceGnmDrawCmdSetRenderTargetMask(&cmd->gnm_cmd, 0xFFFFFFFF);
    }

    /* Re-emit depth/stencil control */
    if (pipe->has_depth_stencil_state) {
        sceGnmDrawCmdSetDepthStencilControl(&cmd->gnm_cmd,
            &pipe->depth_stencil_control);
        if (pipe->depth_stencil_control.stencilenable) {
            vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                R_02842C_DB_STENCIL_CONTROL, pipe->stencil_control);
            vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                R_028430_DB_STENCILREFMASK, pipe->stencil_refmask);
            if (pipe->depth_stencil_control.separatestencilenable) {
                vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                    R_028434_DB_STENCILREFMASK_BF, pipe->stencil_refmask_bf);
            }
        }
        if (pipe->depth_stencil_control.depthboundsenable) {
            vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                R_028020_DB_DEPTH_BOUNDS_MIN,
                vk_ps4_fui(pipe->depth_stencil_state.minDepthBounds));
            vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                R_028024_DB_DEPTH_BOUNDS_MAX,
                vk_ps4_fui(pipe->depth_stencil_state.maxDepthBounds));
        }
    } else {
        GnmDepthStencilControl ds_off;
        memset(&ds_off, 0, sizeof(ds_off));
        sceGnmDrawCmdSetDepthStencilControl(&cmd->gnm_cmd, &ds_off);
    }
}

/* Helper: compute bytes-per-pixel for a VkFormat (for copy operations).
 * For compressed formats, returns bytes-per-block (not bytes-per-pixel).
 * Use vk_format_is_compressed() to detect block-compressed formats. */
static uint32_t vk_format_to_bpp(VkFormat fmt) {
    switch (fmt) {
    case VK_FORMAT_R8_UNORM:           return 1;
    case VK_FORMAT_R8_SNORM:           return 1;
    case VK_FORMAT_R8_UINT:            return 1;
    case VK_FORMAT_R8_SINT:            return 1;
    case VK_FORMAT_R8_SRGB:            return 1;
    case VK_FORMAT_R8G8_UNORM:         return 2;
    case VK_FORMAT_R8G8_SNORM:         return 2;
    case VK_FORMAT_R8G8_UINT:          return 2;
    case VK_FORMAT_R8G8_SINT:          return 2;
    case VK_FORMAT_R8G8_SRGB:          return 2;
    case VK_FORMAT_R8G8B8_UNORM:       return 3;
    case VK_FORMAT_R8G8B8A8_UNORM:     return 4;
    case VK_FORMAT_B8G8R8A8_UNORM:     return 4;
    case VK_FORMAT_R8G8B8A8_SNORM:     return 4;
    case VK_FORMAT_R8G8B8A8_UINT:      return 4;
    case VK_FORMAT_R8G8B8A8_SINT:      return 4;
    case VK_FORMAT_R8G8B8A8_SRGB:      return 4;
    case VK_FORMAT_B8G8R8A8_SRGB:      return 4;
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32: return 4;
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:  return 4;
    case VK_FORMAT_R16_SFLOAT:         return 2;
    case VK_FORMAT_R16_UNORM:          return 2;
    case VK_FORMAT_R16G16_SFLOAT:      return 4;
    case VK_FORMAT_R16G16_UNORM:       return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
    case VK_FORMAT_R16G16B16A16_UNORM:  return 8;
    case VK_FORMAT_R32_SFLOAT:         return 4;
    case VK_FORMAT_R32_UINT:           return 4;
    case VK_FORMAT_R32G32_SFLOAT:      return 8;
    case VK_FORMAT_R32G32_UINT:        return 8;
    case VK_FORMAT_R32G32B32_SFLOAT:   return 12;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
    case VK_FORMAT_R32G32B32A32_UINT:   return 16;
    case VK_FORMAT_D16_UNORM:          return 2;
    case VK_FORMAT_D32_SFLOAT:         return 4;
    case VK_FORMAT_D24_UNORM_S8_UINT:  return 4;
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return 8;
    /* BC formats: bytes per 4x4 block */
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return 8;
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:  return 8;
    case VK_FORMAT_BC2_UNORM_BLOCK:      return 16;
    case VK_FORMAT_BC3_UNORM_BLOCK:      return 16;
    case VK_FORMAT_BC7_UNORM_BLOCK:      return 16;
    case VK_FORMAT_BC7_SRGB_BLOCK:       return 16;
    default:                           return 4;
    }
}

/* Helper: check if a format is block-compressed (4x4 blocks). */
static bool vk_format_is_compressed(VkFormat fmt) {
    switch (fmt) {
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VK_FORMAT_BC2_UNORM_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:
    case VK_FORMAT_BC7_UNORM_BLOCK:
    case VK_FORMAT_BC7_SRGB_BLOCK:
        return true;
    default:
        return false;
    }
}

/* Helper: compute row size in bytes for a given format and width.
 * For uncompressed formats: width * bpp.
 * For compressed formats: (width / 4) * bytes_per_block (rounded up). */
static uint32_t vk_format_row_size(VkFormat fmt, uint32_t width) {
    uint32_t bpp = vk_format_to_bpp(fmt);
    if (vk_format_is_compressed(fmt)) {
        uint32_t blocks_w = (width + 3) / 4;
        return blocks_w * bpp;
    }
    return width * bpp;
}

/* === Command pool === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool) {
    if (!device || !pCreateInfo || !pCommandPool) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4CommandPool *pool = vk_ps4_alloc_zero(alloc, sizeof(*pool), 16);
    if (!pool) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->type = VK_PS4_OBJ_COMMAND_POOL;
    pool->device = dev;
    pool->queue_family_index = pCreateInfo->queueFamilyIndex;
    pool->flags = pCreateInfo->flags;
    *pCommandPool = (VkCommandPool)pool;
    return VK_SUCCESS;
}

/* Record directly into GPU-visible storage: PM4 can contain pointers into
 * itself, so relocating only at submit is not safe for general workloads. */
static VkResult vk_ps4_allocate_pm4(VkPs4CommandBuffer *cmd, const VkAllocationCallbacks *alloc) {
    size_t bytes = (size_t)cmd->pm4_buffer_size * sizeof(uint32_t);
#ifdef VK_USE_PLATFORM_PS4
    (void)alloc;
    if (sceGnmDirectMemoryAllocate(&cmd->pm4_memory, bytes, 65536,
            GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW) != GNM_ERROR_OK)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    cmd->pm4_buffer = cmd->pm4_memory.mapped;
    memset(cmd->pm4_buffer, 0, bytes);
    vk_ps4_log("PM4 direct allocation: address=%p bytes=%zu", (void*)cmd->pm4_buffer, bytes);
#else
    cmd->pm4_buffer = vk_ps4_alloc_zero(alloc, bytes, 256);
    if (!cmd->pm4_buffer) return VK_ERROR_OUT_OF_HOST_MEMORY;
#endif
    return VK_SUCCESS;
}

static void vk_ps4_untrack_failed_command(VkPs4CommandPool *pool, VkPs4CommandBuffer *cmd) {
    if (!pool) return;
    for (uint32_t k = 0; k < pool->command_buffer_count; ++k) {
        if (pool->command_buffers[k] == cmd) {
            --pool->command_buffer_count;
            pool->command_buffers[k] = pool->command_buffers[pool->command_buffer_count];
            pool->command_buffers[pool->command_buffer_count] = NULL;
            return;
        }
    }
}

static void vk_ps4_release_pm4(VkPs4CommandBuffer *cmd, const VkAllocationCallbacks *alloc) {
    if (cmd->pm4_memory.allocated)
        sceGnmDirectMemoryRelease(&cmd->pm4_memory);
    else if (cmd->pm4_buffer)
        vk_ps4_free(alloc, cmd->pm4_buffer);
    cmd->pm4_buffer = NULL;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator) {
    if (!device || !commandPool) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4CommandPool *pool = (VkPs4CommandPool *)commandPool;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    /* Free all command buffers allocated from this pool */
    for (uint32_t i = 0; i < pool->command_buffer_count; i++) {
        VkPs4CommandBuffer *cmd = pool->command_buffers[i];
        if (cmd) {
            if (cmd->pm4_buffer) vk_ps4_release_pm4(cmd, alloc);
            if (cmd->barrier_label.allocated) sceGnmDirectMemoryRelease(&cmd->barrier_label);
            for (unsigned rb = 0; rb < VK_PS4_MAX_READBACK_SCRATCH; ++rb)
                if (cmd->readback_scratch[rb].allocated) sceGnmDirectMemoryRelease(&cmd->readback_scratch[rb]);
            if (cmd->vertex_tables.allocated) sceGnmDirectMemoryRelease(&cmd->vertex_tables);
            vk_ps4_free(alloc, cmd);
            pool->command_buffers[i] = NULL;
        }
    }
    pool->command_buffer_count = 0;

    /* Free all command buffers in the free list */
    for (uint32_t i = 0; i < pool->free_count; i++) {
        if (pool->free_list[i]) {
            if (pool->free_list[i]->pm4_buffer)
                vk_ps4_release_pm4(pool->free_list[i], alloc);
            if (pool->free_list[i]->barrier_label.allocated) sceGnmDirectMemoryRelease(&pool->free_list[i]->barrier_label);
            for (unsigned rb = 0; rb < VK_PS4_MAX_READBACK_SCRATCH; ++rb)
                if (pool->free_list[i]->readback_scratch[rb].allocated) sceGnmDirectMemoryRelease(&pool->free_list[i]->readback_scratch[rb]);
            if (pool->free_list[i]->vertex_tables.allocated) sceGnmDirectMemoryRelease(&pool->free_list[i]->vertex_tables);
            vk_ps4_free(alloc, pool->free_list[i]);
            pool->free_list[i] = NULL;
        }
    }
    pool->free_count = 0;

    vk_ps4_free(alloc, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo,
                              VkCommandBuffer *pCommandBuffers) {
    if (!device || !pAllocateInfo || !pCommandBuffers) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;
    VkPs4CommandPool *pool = (VkPs4CommandPool *)pAllocateInfo->commandPool;

    for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++) {
        VkPs4CommandBuffer *cmd = NULL;

        /* Try to reuse from the pool's free list first */
        if (pool && pool->free_count > 0) {
            cmd = pool->free_list[--pool->free_count];
            pool->free_list[pool->free_count] = NULL;
            /* Reset the command buffer state for reuse */
            memset(cmd, 0, offsetof(VkPs4CommandBuffer, gnm_cmd));
            cmd->pm4_used = 0;
        }

        if (!cmd) {
            cmd = vk_ps4_alloc_zero(alloc, sizeof(*cmd), 16);
            if (!cmd) {
                for (uint32_t j = 0; j < i; j++) {
                    VkPs4CommandBuffer *c = (VkPs4CommandBuffer *)pCommandBuffers[j];
                    if (c) {
                        vk_ps4_untrack_failed_command(pool, c);
                        if (c->pm4_buffer) vk_ps4_release_pm4(c, alloc);
                        if (c->barrier_label.allocated) sceGnmDirectMemoryRelease(&c->barrier_label);
                        for (unsigned rb = 0; rb < VK_PS4_MAX_READBACK_SCRATCH; ++rb)
                if (c->readback_scratch[rb].allocated) sceGnmDirectMemoryRelease(&c->readback_scratch[rb]);
                        if (c->vertex_tables.allocated) sceGnmDirectMemoryRelease(&c->vertex_tables);
                        vk_ps4_free(alloc, c);
                    }
                    pCommandBuffers[j] = VK_NULL_HANDLE;
                }
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            cmd->pm4_buffer = NULL;
        }

        cmd->type = VK_PS4_OBJ_COMMAND_BUFFER;
        cmd->device = dev;
        cmd->pool = pool;
        cmd->level = pAllocateInfo->level;
        cmd->is_recording = false;
        cmd->is_begin = false;
        cmd->current_pipeline = NULL;
        cmd->graphics_set0 = NULL;
        cmd->resource_error = VK_SUCCESS;
        cmd->vertex_binding_count = 0;
    cmd->vertex_tables_used = 0;
    cmd->readback_count = 0;

        /* Allocate PM4 buffer if not reused */
        if (!cmd->pm4_buffer) {
            cmd->pm4_buffer_size = VK_PS4_CMD_BUFFER_SIZE / sizeof(uint32_t);
            VkResult pm4_result = vk_ps4_allocate_pm4(cmd, alloc);
            if (!cmd->pm4_buffer) {
                if (cmd->barrier_label.allocated) sceGnmDirectMemoryRelease(&cmd->barrier_label);
                for (unsigned rb = 0; rb < VK_PS4_MAX_READBACK_SCRATCH; ++rb)
                if (cmd->readback_scratch[rb].allocated) sceGnmDirectMemoryRelease(&cmd->readback_scratch[rb]);
                if (cmd->vertex_tables.allocated) sceGnmDirectMemoryRelease(&cmd->vertex_tables);
            vk_ps4_free(alloc, cmd);
                for (uint32_t j = 0; j < i; j++) {
                    VkPs4CommandBuffer *c = (VkPs4CommandBuffer *)pCommandBuffers[j];
                    if (c) {
                        vk_ps4_untrack_failed_command(pool, c);
                        if (c->pm4_buffer) vk_ps4_release_pm4(c, alloc);
                        if (c->barrier_label.allocated) sceGnmDirectMemoryRelease(&c->barrier_label);
                        for (unsigned rb = 0; rb < VK_PS4_MAX_READBACK_SCRATCH; ++rb)
                if (c->readback_scratch[rb].allocated) sceGnmDirectMemoryRelease(&c->readback_scratch[rb]);
                        if (c->vertex_tables.allocated) sceGnmDirectMemoryRelease(&c->vertex_tables);
                        vk_ps4_free(alloc, c);
                    }
                    pCommandBuffers[j] = VK_NULL_HANDLE;
                }
                return pm4_result;
            }
        } else {
            /* Reused buffer — clear the PM4 contents */
            memset(cmd->pm4_buffer, 0, cmd->pm4_buffer_size * sizeof(uint32_t));
        }
        cmd->pm4_used = 0;

        /* Register in pool for cleanup. If pool is full, fail to avoid leak. */
        if (pool) {
            if (pool->command_buffer_count < VK_PS4_MAX_COMMAND_BUFFERS_PER_POOL) {
                pool->command_buffers[pool->command_buffer_count++] = cmd;
            } else {
                vk_ps4_release_pm4(cmd, alloc);
                if (cmd->barrier_label.allocated) sceGnmDirectMemoryRelease(&cmd->barrier_label);
                for (unsigned rb = 0; rb < VK_PS4_MAX_READBACK_SCRATCH; ++rb)
                if (cmd->readback_scratch[rb].allocated) sceGnmDirectMemoryRelease(&cmd->readback_scratch[rb]);
                if (cmd->vertex_tables.allocated) sceGnmDirectMemoryRelease(&cmd->vertex_tables);
            vk_ps4_free(alloc, cmd);
                for (uint32_t j = 0; j < i; j++) {
                    VkPs4CommandBuffer *c = (VkPs4CommandBuffer *)pCommandBuffers[j];
                    if (c) {
                        vk_ps4_untrack_failed_command(pool, c);
                        if (c->pm4_buffer) vk_ps4_release_pm4(c, alloc);
                        if (c->barrier_label.allocated) sceGnmDirectMemoryRelease(&c->barrier_label);
                        for (unsigned rb = 0; rb < VK_PS4_MAX_READBACK_SCRATCH; ++rb)
                if (c->readback_scratch[rb].allocated) sceGnmDirectMemoryRelease(&c->readback_scratch[rb]);
                        if (c->vertex_tables.allocated) sceGnmDirectMemoryRelease(&c->vertex_tables);
                        vk_ps4_free(alloc, c);
                    }
                    pCommandBuffers[j] = VK_NULL_HANDLE;
                }
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
        }

        pCommandBuffers[i] = (VkCommandBuffer)cmd;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                          uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers) {
    if (!device || !pCommandBuffers) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;
    VkPs4CommandPool *pool = (VkPs4CommandPool *)commandPool;

    for (uint32_t i = 0; i < commandBufferCount; i++) {
        if (!pCommandBuffers[i]) continue;
        VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)pCommandBuffers[i];

        /* Remove from pool's tracking array to avoid double-free in DestroyCommandPool */
        if (pool) {
            for (uint32_t j = 0; j < pool->command_buffer_count; j++) {
                if (pool->command_buffers[j] == cmd) {
                    pool->command_buffers[j] = NULL;
                    /* Compact: move last element into the gap */
                    if (j < pool->command_buffer_count - 1) {
                        pool->command_buffers[j] = pool->command_buffers[pool->command_buffer_count - 1];
                        pool->command_buffers[pool->command_buffer_count - 1] = NULL;
                    }
                    pool->command_buffer_count--;
                    break;
                }
            }
        }

        /* Add to the pool's free list for reuse, or free if pool is full */
        if (pool && pool->free_count < VK_PS4_MAX_COMMAND_BUFFERS_PER_POOL) {
            pool->free_list[pool->free_count++] = cmd;
        } else {
            if (cmd->pm4_buffer) vk_ps4_release_pm4(cmd, alloc);
            if (cmd->barrier_label.allocated) sceGnmDirectMemoryRelease(&cmd->barrier_label);
            for (unsigned rb = 0; rb < VK_PS4_MAX_READBACK_SCRATCH; ++rb)
                if (cmd->readback_scratch[rb].allocated) sceGnmDirectMemoryRelease(&cmd->readback_scratch[rb]);
            if (cmd->vertex_tables.allocated) sceGnmDirectMemoryRelease(&cmd->vertex_tables);
            vk_ps4_free(alloc, cmd);
        }
    }
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo) {
    if (pBeginInfo && (pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    if (!commandBuffer) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    if (!cmd->pm4_buffer) return VK_ERROR_INITIALIZATION_FAILED;

    /* Initialize GnmCommandBuffer with the PM4 buffer */
    cmd->gnm_cmd = sceGnmCmdInit(
        cmd->pm4_buffer, cmd->pm4_buffer_size * sizeof(uint32_t), NULL, NULL
    );

    /* Emit default hardware state only for primary command buffers.
     * Secondary buffers are executed within a primary's context, so they
     * inherit the default state from the primary. */
    if (cmd->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
        sceGnmDrawCmdInitDefaultHardwareState(&cmd->gnm_cmd);
    }

    cmd->viewport0_valid = false;
    cmd->screen_scissor_valid = false;
    cmd->is_recording = true;
    cmd->is_begin = true;
    cmd->pm4_used = (uint32_t)(cmd->gnm_cmd.cmdptr - cmd->gnm_cmd.beginptr);
    cmd->diagnostic_count = 0;
    if (vk_ps4_operation_diagnostic()) {
        cmd->diagnostic_offsets[0] = 0;
        cmd->diagnostic_names[0] = "INIT";
        cmd->diagnostic_count = 1;
    }
    /* Reset all tracking state */
    cmd->current_pipeline = NULL;
    cmd->graphics_set0 = NULL;
    memset(cmd->graphics_push_valid,0,sizeof(cmd->graphics_push_valid));
    cmd->resource_error = VK_SUCCESS;
    cmd->vertex_binding_count = 0;
    cmd->vertex_tables_used = 0;
    cmd->readback_count = 0;
    cmd->vertex_buffers_dirty = false;
    memset(&cmd->index_buffer, 0, sizeof(cmd->index_buffer));
    memset(cmd->vertex_buffers, 0, sizeof(cmd->vertex_buffers));
    memset(cmd->gnm_vertex_buffers, 0, sizeof(cmd->gnm_vertex_buffers));
    memset(&cmd->current_render_pass, 0, sizeof(cmd->current_render_pass));
    /* Reset stencil shadow state */
    cmd->stencil_refmask_front = 0;
    cmd->stencil_refmask_back = 0;
    cmd->stencil_shadow_valid = false;

    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_EndCommandBuffer(VkCommandBuffer commandBuffer) {
    if (!commandBuffer) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    cmd->is_recording = false;
    cmd->pm4_used = (uint32_t)(cmd->gnm_cmd.cmdptr - cmd->gnm_cmd.beginptr);
    vk_ps4_log("EndCommandBuffer: result=%d dwords=%u capacity=%u", cmd->resource_error, cmd->pm4_used, cmd->pm4_buffer_size);
    return cmd->resource_error;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) {
    (void)flags;
    if (!commandBuffer) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    if (cmd->pm4_buffer && cmd->gnm_cmd.beginptr) {
        sceGnmCmdReset(&cmd->gnm_cmd);
    }
    cmd->pm4_used = 0;
    cmd->is_recording = false;
    cmd->diagnostic_count = 0;
    if (vk_ps4_operation_diagnostic()) {
        cmd->diagnostic_offsets[0] = 0;
        cmd->diagnostic_names[0] = "INIT";
        cmd->diagnostic_count = 1;
    }
    /* Reset all tracking state */
    cmd->current_pipeline = NULL;
    cmd->graphics_set0 = NULL;
    memset(cmd->graphics_push_valid,0,sizeof(cmd->graphics_push_valid));
    cmd->resource_error = VK_SUCCESS;
    cmd->vertex_binding_count = 0;
    cmd->vertex_tables_used = 0;
    cmd->readback_count = 0;
    cmd->vertex_buffers_dirty = false;
    memset(&cmd->index_buffer, 0, sizeof(cmd->index_buffer));
    memset(cmd->vertex_buffers, 0, sizeof(cmd->vertex_buffers));
    memset(cmd->gnm_vertex_buffers, 0, sizeof(cmd->gnm_vertex_buffers));
    memset(&cmd->current_render_pass, 0, sizeof(cmd->current_render_pass));
    /* Reset stencil shadow state */
    cmd->stencil_refmask_front = 0;
    cmd->stencil_refmask_back = 0;
    cmd->stencil_shadow_valid = false;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags) {
    (void)flags;
    if (!device || !commandPool) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;
    VkPs4CommandPool *pool = (VkPs4CommandPool *)commandPool;

    /* Free all command buffers in the free list */
    for (uint32_t i = 0; i < pool->free_count; i++) {
        if (pool->free_list[i]) {
            if (pool->free_list[i]->pm4_buffer)
                vk_ps4_release_pm4(pool->free_list[i], alloc);
            if (pool->free_list[i]->barrier_label.allocated) sceGnmDirectMemoryRelease(&pool->free_list[i]->barrier_label);
            for (unsigned rb = 0; rb < VK_PS4_MAX_READBACK_SCRATCH; ++rb)
                if (pool->free_list[i]->readback_scratch[rb].allocated) sceGnmDirectMemoryRelease(&pool->free_list[i]->readback_scratch[rb]);
            if (pool->free_list[i]->vertex_tables.allocated) sceGnmDirectMemoryRelease(&pool->free_list[i]->vertex_tables);
            vk_ps4_free(alloc, pool->free_list[i]);
            pool->free_list[i] = NULL;
        }
    }
    pool->free_count = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_TrimCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags) {
    (void)flags;
    if (!device || !commandPool) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;
    VkPs4CommandPool *pool = (VkPs4CommandPool *)commandPool;

    /* Free all command buffers in the free list — they're not in use */
    for (uint32_t i = 0; i < pool->free_count; i++) {
        if (pool->free_list[i]) {
            if (pool->free_list[i]->pm4_buffer)
                vk_ps4_release_pm4(pool->free_list[i], alloc);
            if (pool->free_list[i]->barrier_label.allocated) sceGnmDirectMemoryRelease(&pool->free_list[i]->barrier_label);
            for (unsigned rb = 0; rb < VK_PS4_MAX_READBACK_SCRATCH; ++rb)
                if (pool->free_list[i]->readback_scratch[rb].allocated) sceGnmDirectMemoryRelease(&pool->free_list[i]->readback_scratch[rb]);
            if (pool->free_list[i]->vertex_tables.allocated) sceGnmDirectMemoryRelease(&pool->free_list[i]->vertex_tables);
            vk_ps4_free(alloc, pool->free_list[i]);
            pool->free_list[i] = NULL;
        }
    }
    pool->free_count = 0;
}

/* === Command buffer recording === */

VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetViewport(VkCommandBuffer, uint32_t, uint32_t, const VkViewport *);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetScissor(VkCommandBuffer, uint32_t, uint32_t, const VkRect2D *);

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) {
    if (!commandBuffer || !pipeline) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_mark_operation(cmd, "BindPipeline");
    VkPs4Pipeline *pipe = (VkPs4Pipeline *)pipeline;

    /* Validate bind point matches pipeline type */
    if (pipe->bind_point != pipelineBindPoint) return;

    cmd->current_pipeline = pipe;

    if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        /* Static viewport/scissor state belongs to the pipeline. Dynamic
         * pipelines retain the values supplied by CmdSetViewport/Scissor. */
        if (!pipe->dynamic_viewport && pipe->viewport_state.viewportCount)
            vk_ps4_CmdSetViewport(commandBuffer, 0, pipe->viewport_state.viewportCount, pipe->static_viewports);
        if (!pipe->dynamic_scissor && pipe->viewport_state.scissorCount)
            vk_ps4_CmdSetScissor(commandBuffer, 0, pipe->viewport_state.scissorCount, pipe->static_scissors);
        /* Set primitive type */
        sceGnmDrawCmdSetPrimitiveType(&cmd->gnm_cmd,
            vk_topology_to_gnm(pipe->input_assembly_state.topology));

        /* Set vertex shader (or LS/ES if tessellation/geometry is active).
         * On GCN, the post-tessellation vertex stage (domain shader / TES
         * compiled as DS_VS) runs on the VS hardware, so SetVsShader must
         * be called for the TES registers when tessellation is active. */
        if (pipe->has_ls) {
            /* Tessellation path: VS is compiled as LS (local shader).
             * SetLsShader sets the LS stage (pre-tessellation vertex). */
            sceGnmDrawCmdSetLsShader(&cmd->gnm_cmd, &pipe->ls_regs, 0);
            /* The TES (domain shader) runs on VS hardware post-tessellation.
             * Emit SetVsShader with the TES registers (stored in vs_regs). */
            if (pipe->has_ds_vs) {
                sceGnmDrawCmdSetVsShader(&cmd->gnm_cmd, &pipe->vs_regs, 0);
            }
        } else if (pipe->has_es && pipe->has_gs) {
            /* GS-only pipeline (no tess): VS compiled as ES.
             * ES is set in the has_gs block below — don't call SetVsShader
             * here because vs_regs is zeroed (VS was compiled as ES). */
        } else if (pipe->has_es && !pipe->has_gs) {
            /* TES compiled as ES (DS_ES path, no GS) */
            sceGnmDrawCmdSetEsShader(&cmd->gnm_cmd, &pipe->es_regs, 0);
        } else {
            /* Standard VS or DS_VS (no tess) */
            sceGnmDrawCmdSetVsShader(&cmd->gnm_cmd, &pipe->vs_regs, 0);
        }

        /* Set hull shader (tessellation control) if present */
        if (pipe->has_hs) {
            /* lshsconfig = VGT_LS_HS_CONFIG register value.
             * HS_NUM_INPUT_CP = patchControlPoints (from Vulkan tess state)
             * HS_NUM_OUTPUT_CP = patchControlPoints (default: same as input
             *   when TCS output CP count is unknown — the shader compiler
             *   may override this in the shader binary) */
            uint32_t cp = pipe->tess_patch_control_points;
            if (cp > 32) cp = 32;  /* 6-bit field, max 32 */
            if (cp == 0) cp = 3;   /* default patch size */
            uint32_t lshsconfig = S_028B58_HS_NUM_INPUT_CP(cp) |
                                   S_028B58_HS_NUM_OUTPUT_CP(cp);
            sceGnmDrawCmdSetHsShader(&cmd->gnm_cmd, &pipe->hs_regs, lshsconfig);
        }

        /* Set geometry shader if present */
        if (pipe->has_gs) {
            sceGnmDrawCmdSetGsShader(&cmd->gnm_cmd, &pipe->gs_regs);
            /* If VS was compiled as ES (for GS path), set ES shader */
            if (pipe->has_es) {
                sceGnmDrawCmdSetEsShader(&cmd->gnm_cmd, &pipe->es_regs, 0);
            }
        }

        /* Set pixel shader only if the pipeline has one */
        if (pipe->has_ps) {
            sceGnmDrawCmdSetPsShader(&cmd->gnm_cmd, &pipe->ps_regs);
            sceGnmDrawCmdSetPsInputUsage(&cmd->gnm_cmd, pipe->vs_exports,
                pipe->vs_export_count, pipe->ps_inputs, pipe->ps_input_count);
        }

        /* Bind fetch shader if the pipeline has one */
        if (pipe->has_fetch_shader && pipe->has_fetch_shader_slot && pipe->fetch_shader) {
            sceGnmDrawCmdSetPointerUserData(
                &cmd->gnm_cmd, GNM_STAGE_VS, pipe->fetch_shader_slot,
                pipe->fetch_shader
            );
        }

        /* Emit blend state + render target mask.
         * This must be done every time a pipeline is bound, because
         * draw-based clears (vk_ps4_clear_color_draw) clobber blend state
         * and RT mask. Without this, the clear's blend state would persist. */
        if (pipe->has_blend_state) {
            /* Set blend constants */
            sceGnmDrawCmdSetBlendColor(&cmd->gnm_cmd,
                pipe->blend_constants[0], pipe->blend_constants[1],
                pipe->blend_constants[2], pipe->blend_constants[3]);

            /* Emit blend control for each RT slot */
            for (uint32_t j = 0; j < pipe->blend_control_count && j < 8; j++) {
                sceGnmDrawCmdSetBlendControl(&cmd->gnm_cmd, j,
                    &pipe->blend_controls[j]);
            }

            /* Set render target mask from colorWriteMask */
            sceGnmDrawCmdSetRenderTargetMask(&cmd->gnm_cmd,
                pipe->color_write_mask);
        } else {
            /* No blend state — disable blending for all RTs, write all channels */
            GnmBlendControl no_blend;
            memset(&no_blend, 0, sizeof(no_blend));
            no_blend.blendenabled = false;
            for (uint32_t j = 0; j < 8; j++) {
                sceGnmDrawCmdSetBlendControl(&cmd->gnm_cmd, j, &no_blend);
            }
            sceGnmDrawCmdSetRenderTargetMask(&cmd->gnm_cmd, 0xFFFFFFFF);
        }

        /* Emit depth/stencil state.
         * When pDepthStencilState is NULL, the Vulkan spec requires that
         * no depth/stencil operations are performed. We must explicitly
         * disable depth/stencil to prevent inheriting state from a
         * previous pipeline. */
        if (pipe->has_depth_stencil_state) {
            /* DB_DEPTH_CONTROL: depth/stencil enable, compare funcs */
            sceGnmDrawCmdSetDepthStencilControl(&cmd->gnm_cmd, &pipe->depth_stencil_control);

            /* Stencil ops, ref/mask (emitted via direct PM4 since GNM API
             * doesn't have wrappers for these registers) */
            if (pipe->depth_stencil_control.stencilenable) {
                vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                    R_02842C_DB_STENCIL_CONTROL, pipe->stencil_control);
                vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                    R_028430_DB_STENCILREFMASK, pipe->stencil_refmask);
                if (pipe->depth_stencil_control.separatestencilenable) {
                    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                        R_028434_DB_STENCILREFMASK_BF, pipe->stencil_refmask_bf);
                }
                /* Initialize shadow state from pipeline so dynamic
                 * stencil commands can do read-modify-write. */
                cmd->stencil_refmask_front = pipe->stencil_refmask;
                cmd->stencil_refmask_back = pipe->stencil_refmask_bf;
                cmd->stencil_shadow_valid = true;
            } else {
                /* Stencil disabled — invalidate shadow state so
                 * CmdSetStencil* won't emit stale values. */
                cmd->stencil_shadow_valid = false;
            }

            /* Depth bounds test */
            if (pipe->depth_stencil_control.depthboundsenable) {
                vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                    R_028020_DB_DEPTH_BOUNDS_MIN,
                    vk_ps4_fui(pipe->depth_stencil_state.minDepthBounds));
                vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                    R_028024_DB_DEPTH_BOUNDS_MAX,
                    vk_ps4_fui(pipe->depth_stencil_state.maxDepthBounds));
            }
        } else {
            /* No depth/stencil state — explicitly disable all DS operations */
            GnmDepthStencilControl ds_off;
            memset(&ds_off, 0, sizeof(ds_off));
            ds_off.depthenable = 0;
            ds_off.zwrite = 0;
            ds_off.stencilenable = 0;
            ds_off.depthboundsenable = 0;
            sceGnmDrawCmdSetDepthStencilControl(&cmd->gnm_cmd, &ds_off);
        }
    } else if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
        sceGnmDrawCmdSetCsShader(&cmd->gnm_cmd, &pipe->cs_regs);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport *pViewports) {
    if (!commandBuffer || !pViewports) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* GNM viewport: scale/offset maps Vulkan viewport to GNM */
    for (uint32_t i = 0; i < viewportCount; i++) {
        const VkViewport *vp = &pViewports[i];
        if (firstViewport + i == 0) { cmd->viewport0 = *vp; cmd->viewport0_valid = true; }
        GnmSetViewportInfo vp_info;
        vp_info.dmin = vp->minDepth;
        vp_info.dmax = vp->maxDepth;
        /* Vulkan viewport: x, y is top-left, width/height extend right/down
         * GNM viewport: scale = half-dimension, offset = center */
        vp_info.scale[0] = vp->width * 0.5f;
        vp_info.scale[1] = vp->height * 0.5f;
        vp_info.scale[2] = vp->maxDepth - vp->minDepth;
        vp_info.offset[0] = vp->x + vp->width * 0.5f;
        vp_info.offset[1] = vp->y + vp->height * 0.5f;
        vp_info.offset[2] = vp->minDepth;
        sceGnmDrawCmdSetViewport(&cmd->gnm_cmd, firstViewport + i, &vp_info);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D *pScissors) {
    if (!commandBuffer || !pScissors) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    for (uint32_t i = 0; i < scissorCount; i++) {
        const VkRect2D *sc = &pScissors[i];
        cmd->screen_scissor = *sc; cmd->screen_scissor_valid = true;
        const uint32_t regs[] = {0x28204, 0x28240, 0x28250};
        for (unsigned j = 0; j < 3; ++j) {
            vk_ps4_emit_context_reg(&cmd->gnm_cmd, regs[j], 0x80000000u |
                (uint32_t)sc->offset.x | ((uint32_t)sc->offset.y << 16));
            vk_ps4_emit_context_reg(&cmd->gnm_cmd, regs[j] + 4,
                (sc->offset.x + sc->extent.width) | ((sc->offset.y + sc->extent.height) << 16));
        }
        sceGnmDrawCmdSetScreenScissor(&cmd->gnm_cmd,
            sc->offset.x, sc->offset.y,
            sc->offset.x + sc->extent.width,
            sc->offset.y + sc->extent.height);
    }
}

/* === Dynamic state commands (Phase 3 Step 30) === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]) {
    if (!commandBuffer || !blendConstants) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    sceGnmDrawCmdSetBlendColor(&cmd->gnm_cmd,
        blendConstants[0], blendConstants[1],
        blendConstants[2], blendConstants[3]);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor,
                       float depthBiasClamp, float depthBiasSlopeFactor) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* GCN polygon offset: scale and offset are in fixed-point format.
     * The hardware expects the float values converted to uint32 bit patterns
     * (the GPU interprets them as floats). */
    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
        R_028B80_PA_SU_POLY_OFFSET_FRONT_SCALE,
        vk_ps4_fui(depthBiasSlopeFactor));
    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
        R_028B84_PA_SU_POLY_OFFSET_FRONT_OFFSET,
        vk_ps4_fui(depthBiasConstantFactor));
    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
        R_028B88_PA_SU_POLY_OFFSET_BACK_SCALE,
        vk_ps4_fui(depthBiasSlopeFactor));
    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
        R_028B8C_PA_SU_POLY_OFFSET_BACK_OFFSET,
        vk_ps4_fui(depthBiasConstantFactor));
    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
        R_028B7C_PA_SU_POLY_OFFSET_CLAMP,
        vk_ps4_fui(depthBiasClamp));
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
        R_028020_DB_DEPTH_BOUNDS_MIN, vk_ps4_fui(minDepthBounds));
    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
        R_028024_DB_DEPTH_BOUNDS_MAX, vk_ps4_fui(maxDepthBounds));
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
                                 uint32_t compareMask) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* DB_STENCILREFMASK: [7:0]=TESTVAL, [15:8]=MASK, [23:16]=WRITEMASK, [31:24]=OPVAL
     * Read-modify-write: only update the MASK field, preserving the others.
     * If no pipeline with stencil enabled has been bound, skip — emitting
     * stale register values would be a spec violation. */
    if (!cmd->stencil_shadow_valid) return;
    if (faceMask & VK_STENCIL_FACE_FRONT_BIT) {
        cmd->stencil_refmask_front = (cmd->stencil_refmask_front & C_028430_STENCILMASK) |
                                      S_028430_STENCILMASK(compareMask & 0xFF);
        vk_ps4_emit_context_reg(&cmd->gnm_cmd,
            R_028430_DB_STENCILREFMASK, cmd->stencil_refmask_front);
    }
    if (faceMask & VK_STENCIL_FACE_BACK_BIT) {
        cmd->stencil_refmask_back = (cmd->stencil_refmask_back & C_028434_STENCILMASK_BF) |
                                     S_028434_STENCILMASK_BF(compareMask & 0xFF);
        vk_ps4_emit_context_reg(&cmd->gnm_cmd,
            R_028434_DB_STENCILREFMASK_BF, cmd->stencil_refmask_back);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
                               uint32_t writeMask) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    if (!cmd->stencil_shadow_valid) return;

    if (faceMask & VK_STENCIL_FACE_FRONT_BIT) {
        cmd->stencil_refmask_front = (cmd->stencil_refmask_front & C_028430_STENCILWRITEMASK) |
                                      S_028430_STENCILWRITEMASK(writeMask & 0xFF);
        vk_ps4_emit_context_reg(&cmd->gnm_cmd,
            R_028430_DB_STENCILREFMASK, cmd->stencil_refmask_front);
    }
    if (faceMask & VK_STENCIL_FACE_BACK_BIT) {
        cmd->stencil_refmask_back = (cmd->stencil_refmask_back & C_028434_STENCILWRITEMASK_BF) |
                                     S_028434_STENCILWRITEMASK_BF(writeMask & 0xFF);
        vk_ps4_emit_context_reg(&cmd->gnm_cmd,
            R_028434_DB_STENCILREFMASK_BF, cmd->stencil_refmask_back);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
                               uint32_t reference) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    if (!cmd->stencil_shadow_valid) return;

    if (faceMask & VK_STENCIL_FACE_FRONT_BIT) {
        cmd->stencil_refmask_front = (cmd->stencil_refmask_front & C_028430_STENCILTESTVAL) |
                                      S_028430_STENCILTESTVAL(reference & 0xFF);
        vk_ps4_emit_context_reg(&cmd->gnm_cmd,
            R_028430_DB_STENCILREFMASK, cmd->stencil_refmask_front);
    }
    if (faceMask & VK_STENCIL_FACE_BACK_BIT) {
        cmd->stencil_refmask_back = (cmd->stencil_refmask_back & C_028434_STENCILTESTVAL_BF) |
                                     S_028434_STENCILTESTVAL_BF(reference & 0xFF);
        vk_ps4_emit_context_reg(&cmd->gnm_cmd,
            R_028434_DB_STENCILREFMASK_BF, cmd->stencil_refmask_back);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* PA_SU_LINE_CNTL: [15:0]=WIDTH (in 4.12 fixed-point format).
     * Convert float pixels to fixed-point: width * 4096.
     * Clamp to valid range. 0xFFFF/4096 ≈ 15.9998 is the max representable
     * value in 4.12 fixed point — 16.0 would be 0x10000 which wraps to 0. */
    if (lineWidth < 0.0f) lineWidth = 0.0f;
    uint32_t width_fixed;
    if (lineWidth >= 16.0f) {
        width_fixed = 0xFFFF;  /* saturate at max representable width */
    } else {
        width_fixed = (uint32_t)(lineWidth * 4096.0f) & 0xFFFF;
    }
    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
        R_028A08_PA_SU_LINE_CNTL, S_028A08_WIDTH(width_fixed));
}

/* CmdBindDescriptorSets moved to vk_ps4_descriptor.c */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding,
                             uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets) {
    if (!commandBuffer || !pBuffers) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    if (firstBinding >= VK_PS4_MAX_VERTEX_BINDINGS || bindingCount > VK_PS4_MAX_VERTEX_BINDINGS - firstBinding) {
        cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
    }
    for (uint32_t i = 0; i < bindingCount; i++) {
        uint32_t idx = firstBinding + i;
        if (idx < VK_PS4_MAX_VERTEX_BINDINGS) {
            cmd->vertex_buffers[idx].buffer = pBuffers[i];
            cmd->vertex_buffers[idx].offset = pOffsets ? pOffsets[i] : 0;


        }
    }
    /* Update vertex_binding_count only for bindings that were actually stored.
     * This avoids inflating the count when firstBinding >= MAX. */
    uint32_t max_idx_stored = cmd->vertex_binding_count;
    for (uint32_t i = 0; i < bindingCount; i++) {
        uint32_t idx = firstBinding + i;
        if (idx < VK_PS4_MAX_VERTEX_BINDINGS && idx + 1 > max_idx_stored) {
            max_idx_stored = idx + 1;
        }
    }
    cmd->vertex_binding_count = max_idx_stored;
    cmd->vertex_buffers_dirty = true;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    if (indexType != VK_INDEX_TYPE_UINT16 && indexType != VK_INDEX_TYPE_UINT32) {
        cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
    }
    cmd->index_buffer.buffer = buffer;
    cmd->index_buffer.offset = offset;
    cmd->index_buffer.type = indexType;

    /* Set index buffer in GNM */
    VkPs4Buffer *buf = (VkPs4Buffer *)buffer;
    if (buf && buf->memory && buf->memory->gnm_mem.mapped) {
        void *gpu_addr = (char *)buf->memory->gnm_mem.mapped + buf->memory_offset + offset;
        sceGnmDrawCmdSetIndexBuffer(&cmd->gnm_cmd, gpu_addr);

        GnmIndexSize idx_size;
        switch (indexType) {
        case VK_INDEX_TYPE_UINT16: idx_size = GNM_INDEX_16; break;
        case VK_INDEX_TYPE_UINT32: idx_size = GNM_INDEX_32; break;
        default: idx_size = GNM_INDEX_16; break;
        }
        sceGnmDrawCmdSetIndexSize(&cmd->gnm_cmd, idx_size, GNM_POLICY_BYPASS);
    }
}

/* Snapshot descriptors per draw: later binds must not mutate earlier draws. */
static bool vk_ps4_emit_vertex_table(VkPs4CommandBuffer *cmd) {
    VkPs4Pipeline *pipe = cmd->current_pipeline;
    if (!pipe || !pipe->vs_input_semantic_count) return true;
    if (!pipe->has_fetch_shader || !pipe->has_vb_table_slot) goto unsupported;
    const VkPipelineVertexInputStateCreateInfo *vi = &pipe->vertex_input_state;
    uint32_t count = pipe->vs_input_semantic_count;
    if (count > VK_PS4_MAX_VERTEX_BINDINGS) goto unsupported;
    GnmBuffer table[VK_PS4_MAX_VERTEX_BINDINGS] = {0};
    for (uint32_t i = 0; i < count; ++i) {
        const VkVertexInputAttributeDescription *attr = NULL;
        const VkVertexInputBindingDescription *binding = NULL;
        for (uint32_t j = 0; j < vi->vertexAttributeDescriptionCount; ++j)
            if (vi->pVertexAttributeDescriptions[j].location == pipe->vs_input_semantics[i].semantic)
                attr = &vi->pVertexAttributeDescriptions[j];
        if (!attr || attr->binding >= VK_PS4_MAX_VERTEX_BINDINGS) goto unsupported;
        for (uint32_t j = 0; j < vi->vertexBindingDescriptionCount; ++j)
            if (vi->pVertexBindingDescriptions[j].binding == attr->binding)
                binding = &vi->pVertexBindingDescriptions[j];
        uint32_t bytes;
        switch (attr->format) {
        case VK_FORMAT_R32_SFLOAT: bytes = 4; break;
        case VK_FORMAT_R32G32_SFLOAT: bytes = 8; break;
        case VK_FORMAT_R32G32B32_SFLOAT: bytes = 12; break;
        case VK_FORMAT_R32G32B32A32_SFLOAT: bytes = 16; break;
        default: goto unsupported;
        }
        if (!binding || binding->inputRate != VK_VERTEX_INPUT_RATE_VERTEX ||
            !binding->stride || binding->stride > 16383 || attr->offset > binding->stride ||
            bytes > binding->stride - attr->offset) goto unsupported;
        VkPs4Buffer *buf = (VkPs4Buffer *)cmd->vertex_buffers[attr->binding].buffer;
        VkDeviceSize offset = cmd->vertex_buffers[attr->binding].offset;
        if (!buf || !buf->memory || !buf->memory->gnm_mem.mapped ||
            offset > buf->create_info.size || attr->offset > buf->create_info.size - offset)
            goto unsupported;
        offset += attr->offset;
        VkDeviceSize remaining = buf->create_info.size - offset;
        if (remaining < bytes || (remaining - bytes) / binding->stride >= UINT32_MAX)
            goto unsupported;
        table[i] = sceGnmCreateVertexBuffer(
            (char *)buf->memory->gnm_mem.mapped + buf->memory_offset + offset,
            vk_ps4_vk_format_to_gnm_buffer(attr->format), binding->stride,
            (uint32_t)((remaining - bytes) / binding->stride + 1));
    }
    uint32_t size = count * sizeof(GnmBuffer);
    if (cmd->vertex_tables_used + size > 65536) goto exhausted;
    if (!cmd->vertex_tables.allocated && sceGnmDirectMemoryAllocate(
            &cmd->vertex_tables, 65536, 65536, GNM_DIRECT_MEMORY_TYPE_WC_GARLIC,
            GNM_PROT_CPU_GPU_RW) != GNM_ERROR_OK) goto exhausted;
    void *snapshot = (char *)cmd->vertex_tables.mapped + cmd->vertex_tables_used;
    memcpy(snapshot, table, size);
    cmd->vertex_tables_used += size;
    sceGnmDrawCmdSetPointerUserData(&cmd->gnm_cmd, GNM_STAGE_VS,
                                  pipe->vertex_buffer_table_slot, snapshot);
    return true;
unsupported:
    cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT;
    return false;
exhausted:
    cmd->resource_error = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    return false;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount,
               uint32_t firstVertex, uint32_t firstInstance) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_mark_operation(cmd, "Draw");
    if (cmd->current_pipeline && cmd->current_pipeline->vs_input_semantic_count &&
        (firstVertex || firstInstance || instanceCount != 1)) {
        cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
    }
    if (!vk_ps4_emit_resource_table(cmd)) return;

    if (!vk_ps4_emit_vertex_table(cmd)) return;

    /* Always set instance count to avoid state leak from previous draw */
    sceGnmDrawCmdSetNumInstances(&cmd->gnm_cmd, instanceCount);

    /* Emit firstVertex and firstInstance via SET_SH_REG to the
     * user-data registers that psbc reserved for base_vertex and
     * start_instance. The shader adds these to gl_VertexIndex and
     * gl_InstanceIndex respectively.
     * GCN user-data registers are sticky (persist across draws), so
     * we must always write them when the pipeline uses them — even
     * when the value is 0 — to avoid stale state from a previous draw.
     * Optimization: if both registers are consecutive, emit a single
     * SET_SH_REG packet with count=2 instead of two separate packets. */
    if (cmd->current_pipeline) {
        VkPs4Pipeline *pipe = cmd->current_pipeline;
        bool both = pipe->has_base_vertex_reg && pipe->has_start_instance_reg &&
                    pipe->vs_base_vertex_reg + 1 == pipe->vs_start_instance_reg;
        if (both) {
            /* Batched: single SET_SH_REG with 2 values */
            uint32_t reg_addr = R_00B130_SPI_SHADER_USER_DATA_VS_0 +
                                pipe->vs_base_vertex_reg * 4;
            if ((uint32_t)(cmd->gnm_cmd.endptr - cmd->gnm_cmd.cmdptr) >= 4) {
                cmd->gnm_cmd.cmdptr[0] = PKT3(PKT3_SET_SH_REG, 2, 0);
                cmd->gnm_cmd.cmdptr[1] = (reg_addr - SI_SH_REG_OFFSET) >> 2;
                cmd->gnm_cmd.cmdptr[2] = firstVertex;
                cmd->gnm_cmd.cmdptr[3] = firstInstance;
                cmd->gnm_cmd.cmdptr += 4;
            }
        } else {
            if (pipe->has_base_vertex_reg) {
                uint32_t reg_addr = R_00B130_SPI_SHADER_USER_DATA_VS_0 +
                                    pipe->vs_base_vertex_reg * 4;
                if ((uint32_t)(cmd->gnm_cmd.endptr - cmd->gnm_cmd.cmdptr) >= 3) {
                    cmd->gnm_cmd.cmdptr[0] = PKT3(PKT3_SET_SH_REG, 1, 0);
                    cmd->gnm_cmd.cmdptr[1] = (reg_addr - SI_SH_REG_OFFSET) >> 2;
                    cmd->gnm_cmd.cmdptr[2] = firstVertex;
                    cmd->gnm_cmd.cmdptr += 3;
                }
            }
            if (pipe->has_start_instance_reg) {
                uint32_t reg_addr = R_00B130_SPI_SHADER_USER_DATA_VS_0 +
                                    pipe->vs_start_instance_reg * 4;
                if ((uint32_t)(cmd->gnm_cmd.endptr - cmd->gnm_cmd.cmdptr) >= 3) {
                    cmd->gnm_cmd.cmdptr[0] = PKT3(PKT3_SET_SH_REG, 1, 0);
                    cmd->gnm_cmd.cmdptr[1] = (reg_addr - SI_SH_REG_OFFSET) >> 2;
                    cmd->gnm_cmd.cmdptr[2] = firstInstance;
                    cmd->gnm_cmd.cmdptr += 3;
                }
            }
        }
    }

    GnmDrawModifier mod = {0};
    sceGnmDrawCmdDrawIndexAuto2(&cmd->gnm_cmd, vertexCount, mod);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount,
                      uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_mark_operation(cmd, "DrawIndexed");
    if (cmd->current_pipeline && cmd->current_pipeline->vs_input_semantic_count &&
        (vertexOffset || firstInstance || instanceCount != 1)) {
        cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
    }
    if (!vk_ps4_emit_resource_table(cmd)) return;

    if (!vk_ps4_emit_vertex_table(cmd)) return;

    /* Always set instance count to avoid state leak from previous draw */
    sceGnmDrawCmdSetNumInstances(&cmd->gnm_cmd, instanceCount);

    /* Emit vertexOffset and firstInstance via SET_SH_REG BEFORE the draw.
     * GCN user-data registers are read at draw time, so they must be
     * written before the draw packet. They are also sticky (persist
     * across draws), so we must always write them when the pipeline
     * uses them — even when the value is 0 — to avoid stale state
     * from a previous draw.
     * Optimization: if both registers are consecutive, emit a single
     * SET_SH_REG packet with count=2 instead of two separate packets. */
    if (cmd->current_pipeline) {
        VkPs4Pipeline *pipe = cmd->current_pipeline;
        bool both = pipe->has_base_vertex_reg && pipe->has_start_instance_reg &&
                    pipe->vs_base_vertex_reg + 1 == pipe->vs_start_instance_reg;
        if (both) {
            uint32_t reg_addr = R_00B130_SPI_SHADER_USER_DATA_VS_0 +
                                pipe->vs_base_vertex_reg * 4;
            if ((uint32_t)(cmd->gnm_cmd.endptr - cmd->gnm_cmd.cmdptr) >= 4) {
                cmd->gnm_cmd.cmdptr[0] = PKT3(PKT3_SET_SH_REG, 2, 0);
                cmd->gnm_cmd.cmdptr[1] = (reg_addr - SI_SH_REG_OFFSET) >> 2;
                cmd->gnm_cmd.cmdptr[2] = (uint32_t)vertexOffset;
                cmd->gnm_cmd.cmdptr[3] = firstInstance;
                cmd->gnm_cmd.cmdptr += 4;
            }
        } else {
            if (pipe->has_base_vertex_reg) {
                uint32_t reg_addr = R_00B130_SPI_SHADER_USER_DATA_VS_0 +
                                    pipe->vs_base_vertex_reg * 4;
                if ((uint32_t)(cmd->gnm_cmd.endptr - cmd->gnm_cmd.cmdptr) >= 3) {
                    cmd->gnm_cmd.cmdptr[0] = PKT3(PKT3_SET_SH_REG, 1, 0);
                    cmd->gnm_cmd.cmdptr[1] = (reg_addr - SI_SH_REG_OFFSET) >> 2;
                    cmd->gnm_cmd.cmdptr[2] = (uint32_t)vertexOffset;
                    cmd->gnm_cmd.cmdptr += 3;
                }
            }
            if (pipe->has_start_instance_reg) {
                uint32_t reg_addr = R_00B130_SPI_SHADER_USER_DATA_VS_0 +
                                    pipe->vs_start_instance_reg * 4;
                if ((uint32_t)(cmd->gnm_cmd.endptr - cmd->gnm_cmd.cmdptr) >= 3) {
                    cmd->gnm_cmd.cmdptr[0] = PKT3(PKT3_SET_SH_REG, 1, 0);
                    cmd->gnm_cmd.cmdptr[1] = (reg_addr - SI_SH_REG_OFFSET) >> 2;
                    cmd->gnm_cmd.cmdptr[2] = firstInstance;
                    cmd->gnm_cmd.cmdptr += 3;
                }
            }
        }
    }

    /* If we have an index buffer bound, use DrawIndex2 or DrawIndexOffset.
     * DrawIndexOffset takes an index offset (firstIndex) directly.
     * vertexOffset is handled by the shader via gl_VertexIndex user-data. */
    VkPs4Buffer *buf = (VkPs4Buffer *)cmd->index_buffer.buffer;
    if (buf && buf->memory && buf->memory->gnm_mem.mapped) {
        uint32_t index_size = cmd->index_buffer.type == VK_INDEX_TYPE_UINT32 ? 4 : 2;
        VkDeviceSize offset = cmd->index_buffer.offset;
        if (offset > buf->create_info.size || offset % index_size ||
            (uint64_t)firstIndex + indexCount > (buf->create_info.size - offset) / index_size) {
            cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
        }
        void *gpu_addr = (char *)buf->memory->gnm_mem.mapped + buf->memory_offset +
                         cmd->index_buffer.offset;
        GnmDrawModifier mod = {0};
        if (firstIndex > 0) {
            /* DrawIndexOffset takes an index offset into the index buffer */
            sceGnmDrawCmdDrawIndexOffset(&cmd->gnm_cmd, firstIndex, indexCount, mod);
        } else {
            sceGnmDrawCmdDrawIndex2(&cmd->gnm_cmd, indexCount, gpu_addr, mod);
        }
    } else {
        cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT;
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                       uint32_t drawCount, uint32_t stride) {
    if (!commandBuffer || !buffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_mark_operation(cmd, "DrawIndirect");
    VkPs4Buffer *buf = (VkPs4Buffer *)buffer;

    if (!vk_ps4_emit_vertex_table(cmd)) return;

    if (!buf->memory || !buf->memory->gnm_mem.mapped) return;

    /* GnmDrawIndirectArgs = { vertexCount, instanceCount, firstVertex, firstInstance }
     * This matches VkDrawIndirectCommand exactly.
     * The GNM API requires SetIndirectArgs first, then DrawIndirect with
     * an offset relative to the args buffer. */
    const GnmDrawIndirectArgs *args_base =
        (const GnmDrawIndirectArgs *)((char *)buf->memory->gnm_mem.mapped +
                                      buf->memory_offset + offset);
    sceGnmDrawCmdSetIndirectArgs(&cmd->gnm_cmd, args_base);

    for (uint32_t i = 0; i < drawCount; i++) {
        /* dataoffset is relative to the args buffer set above */
        uint32_t data_offset = i * stride;
        /* DrawIndirect reads from GPU memory at the given offset.
         * The vertexoffusgpr and instanceoffusgpr specify which VGPRs
         * contain the vertex/instance offsets. For MVP, use 0. */
        sceGnmDrawCmdDrawIndirect(
            &cmd->gnm_cmd, data_offset, GNM_STAGE_VS, 0, 0
        );
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                              uint32_t drawCount, uint32_t stride) {
    if (!commandBuffer || !buffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_mark_operation(cmd, "DrawIndexedIndirect");
    VkPs4Buffer *buf = (VkPs4Buffer *)buffer;

    if (!vk_ps4_emit_vertex_table(cmd)) return;

    if (!buf->memory || !buf->memory->gnm_mem.mapped) return;

    /* GnmDrawIndexedIndirectArgs = { indexCount, instanceCount, firstIndex, vertexOffset, firstInstance }
     * This matches VkDrawIndexedIndirectCommand.
     * The GNM API requires SetIndexedIndirectArgs first, then DrawIndexIndirect
     * with an offset relative to the args buffer. */
    const GnmDrawIndexedIndirectArgs *args_base =
        (const GnmDrawIndexedIndirectArgs *)((char *)buf->memory->gnm_mem.mapped +
                                             buf->memory_offset + offset);
    sceGnmDrawCmdSetIndexedIndirectArgs(&cmd->gnm_cmd, args_base);

    for (uint32_t i = 0; i < drawCount; i++) {
        uint32_t data_offset = i * stride;
        sceGnmDrawCmdDrawIndexIndirect(
            &cmd->gnm_cmd, data_offset, GNM_STAGE_VS, 0, 0
        );
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    sceGnmDrawCmdDispatchDirect(&cmd->gnm_cmd, x, y, z, 0);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) {
    if (!commandBuffer || !buffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Buffer *buf = (VkPs4Buffer *)buffer;
    if (!buf || !buf->memory || !buf->memory->gnm_mem.mapped) return;

    /* VkDispatchIndirectCommand = { uint32_t x; uint32_t y; uint32_t z; }
     *
     * Unlike DrawIndirect (which uses SetIndirectArgs + relative offset),
     * DISPATCH_INDIRECT takes a GPU memory address directly as the
     * dataoffset. The CP reads 3 uint32s (x, y, z) from that address.
     * The address is 32-bit. PS4 GPU memory is typically mapped in the
     * lower 4GB, but if the buffer's address exceeds 32 bits we stage
     * the 12-byte dispatch args inside the command buffer via
     * sceGnmCmdAllocInside and dispatch from there. */
    uint64_t gpu_addr = (uint64_t)((char *)buf->memory->gnm_mem.mapped +
                                    buf->memory_offset + offset);
    if (gpu_addr <= 0xFFFFFFFFULL) {
        sceGnmDrawCmdDispatchIndirect(&cmd->gnm_cmd, (uint32_t)gpu_addr, 0);
        return;
    }

    /* Address > 32 bits: stage the 12-byte VkDispatchIndirectCommand into
     * command buffer memory (which is always 32-bit addressable) and
     * dispatch from the staging copy. */
    void *src = (char *)buf->memory->gnm_mem.mapped + buf->memory_offset + offset;
    void *staging = sceGnmCmdAllocInside(&cmd->gnm_cmd, 12, 4);
    if (!staging) {
        /* AllocInside failed — cannot dispatch. This is a driver-internal
         * failure, not a spec violation. Log and skip rather than crash. */
        return;
    }
    memcpy(staging, src, 12);
    uint64_t staging_addr = (uint64_t)staging;
    /* staging_addr comes from AllocInside which is always in the 32-bit
     * command buffer address space, so the cast is safe. */
    sceGnmDrawCmdDispatchIndirect(&cmd->gnm_cmd, (uint32_t)staging_addr, 0);
}

/* === Copy/blit commands (Phase 2) === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer,
                     uint32_t regionCount, const VkBufferCopy *pRegions) {
    if (!commandBuffer || !pRegions) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_mark_operation(cmd, "CopyBuffer");
    VkPs4Buffer *src = (VkPs4Buffer *)srcBuffer;
    VkPs4Buffer *dst = (VkPs4Buffer *)dstBuffer;

    if (!src || !dst || !src->memory || !dst->memory) return;
    if (!src->memory->gnm_mem.mapped || !dst->memory->gnm_mem.mapped) return;

    for (uint32_t i = 0; i < regionCount; i++) {
        uint64_t src_addr = (uint64_t)((char *)src->memory->gnm_mem.mapped +
                                        src->memory_offset + pRegions[i].srcOffset);
        uint64_t dst_addr = (uint64_t)((char *)dst->memory->gnm_mem.mapped +
                                        dst->memory_offset + pRegions[i].dstOffset);
        /* sceGnmDrawCmdCopyMemory takes uint32_t size — split large copies */
        uint64_t remaining = pRegions[i].size;
        uint64_t cur_src = src_addr;
        uint64_t cur_dst = dst_addr;
        while (remaining > 0) {
            uint32_t chunk = (remaining > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)remaining;
            sceGnmDrawCmdCopyMemory(&cmd->gnm_cmd, cur_dst, cur_src, chunk);
            cur_src += chunk;
            cur_dst += chunk;
            remaining -= chunk;
        }
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                     VkDeviceSize dstOffset, VkDeviceSize fillSize, uint32_t data) {
    if (!commandBuffer || !dstBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_mark_operation(cmd, "FillBuffer");
    VkPs4Buffer *dst = (VkPs4Buffer *)dstBuffer;
    if (!dst || !dst->memory || !dst->memory->gnm_mem.mapped) return;

    uint64_t dst_addr = (uint64_t)((char *)dst->memory->gnm_mem.mapped +
                                    dst->memory_offset + dstOffset);

    /* VK_WHOLE_SIZE means fill from dstOffset to the end of the buffer.
     * Guard against dstOffset > buffer size to prevent unsigned underflow. */
    if (dstOffset >= dst->create_info.size) return;
    uint64_t size = fillSize;
    if (fillSize == VK_WHOLE_SIZE) {
        size = dst->create_info.size - dstOffset;
    }
    /* Clamp to remaining buffer space */
    if (dstOffset + size > dst->create_info.size) {
        size = dst->create_info.size - dstOffset;
    }
    /* Align size to 4 bytes (FillMemory writes 32-bit values) */
    size = (size + 3) & ~3ULL;

    /* sceGnmDrawCmdFillMemory takes uint32_t size — split large fills */
    uint64_t remaining = size;
    uint64_t cur = dst_addr;
    while (remaining > 0) {
        uint32_t chunk = (remaining > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)remaining;
        /* Align chunk to 4 bytes */
        chunk &= ~3u;
        if (chunk == 0) break;
        sceGnmDrawCmdFillMemory(&cmd->gnm_cmd, cur, chunk, data);
        cur += chunk;
        remaining -= chunk;
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                       VkDeviceSize dstOffset, VkDeviceSize dataSize, const void *pData) {
    if (!commandBuffer || !dstBuffer || !pData) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_mark_operation(cmd, "UpdateBuffer");
    VkPs4Buffer *dst = (VkPs4Buffer *)dstBuffer;
    if (!dst || !dst->memory || !dst->memory->gnm_mem.mapped) return;

    uint64_t dst_addr = (uint64_t)((char *)dst->memory->gnm_mem.mapped +
                                    dst->memory_offset + dstOffset);

    /* CmdUpdateBuffer is limited to 65536 bytes per the Vulkan spec.
     *
     * Phase 3: Stage the data inside the command buffer via
     * sceGnmCmdAllocInside, which embeds the data in the GPU-visible
     * command buffer memory (wrapped in a NOP packet so the CP skips it).
     * Then emit a CopyMemory to copy from the staging area to the
     * destination at submit time.  This is semantically correct — the
     * update happens when the command buffer is executed, not at record
     * time.
     *
     * Fallback: if AllocInside fails (command buffer full), fall back to
     * the old CPU memcpy approach.  This is safe when the destination is
     * not being read by the GPU. */
    uint64_t size = dataSize;
    if (size > 65536) size = 65536;  /* clamp to spec limit */
    if (size == 0) return;

    /* Round up to 4 bytes for AllocInside alignment. */
    uint32_t alloc_size = (uint32_t)((size + 3) & ~3ull);
    void *staging = sceGnmCmdAllocInside(&cmd->gnm_cmd, alloc_size, 4);
    if (staging) {
        /* Copy the caller's data into the staging area (CPU-visible
         * command buffer memory), then emit a GPU copy from staging to
         * the destination.  The copy executes at submit time. */
        memcpy(staging, pData, (size_t)size);
        uint64_t src_addr = (uint64_t)staging;
        /* CopyMemory takes uint32_t size — split large copies. */
        uint64_t remaining = size;
        uint64_t cur_src = src_addr;
        uint64_t cur_dst = dst_addr;
        while (remaining > 0) {
            uint32_t chunk = (remaining > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)remaining;
            sceGnmDrawCmdCopyMemory(&cmd->gnm_cmd, cur_dst, cur_src, chunk);
            cur_src += chunk;
            cur_dst += chunk;
            remaining -= chunk;
        }
    } else {
        /* Fallback: CPU memcpy at record time (semantically incorrect
         * but safe when the GPU is not reading the destination). */
        memcpy((void *)dst_addr, pData, (size_t)size);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                    VkImage dstImage, VkImageLayout dstImageLayout,
                    uint32_t regionCount, const VkImageCopy *pRegions) {
    if (!commandBuffer || !pRegions) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_mark_operation(cmd, "CopyImage");
    VkPs4Image *src = (VkPs4Image *)srcImage;
    VkPs4Image *dst = (VkPs4Image *)dstImage;
    if (!src || !dst) return;

    /* Linear-to-linear image copy via per-row CopyMemory.
     * Tiled texture copies require a shader-based blit or CP DMA with
     * surface info — deferred to a future shader-blit phase. */
    for (uint32_t i = 0; i < regionCount; i++) {
        const VkImageCopy *r = &pRegions[i];

        /* Calculate bytes-per-pixel (or bytes-per-block for compressed). */
        uint32_t bpp = vk_format_to_bpp(src->create_info.format);
        bool compressed = vk_format_is_compressed(src->create_info.format);

        /* Use mip-level-specific dimensions for pitch calculation.
         * For mip level N, the width/height are max(1, extent >> N). */
        uint32_t src_mip_w = src->create_info.extent.width >> r->srcSubresource.mipLevel;
        uint32_t dst_mip_w = dst->create_info.extent.width >> r->dstSubresource.mipLevel;
        if (src_mip_w == 0) src_mip_w = 1;
        if (dst_mip_w == 0) dst_mip_w = 1;

        /* For compressed formats, pitch = (width/4) * bytes_per_block.
         * For uncompressed, pitch = width * bpp. */
        uint32_t src_pitch = vk_format_row_size(src->create_info.format, src_mip_w);
        uint32_t dst_pitch = vk_format_row_size(dst->create_info.format, dst_mip_w);
        uint32_t copy_width = vk_format_row_size(src->create_info.format, r->extent.width);
        uint32_t copy_height = compressed ? (r->extent.height + 3) / 4 : r->extent.height;
        /* Depth/array slice count — iterate over all slices in the region.
         * srcSubresource.layerCount == VK_REMAINING_ARRAY_LAYERS means copy
         * all layers from baseArrayLayer to the image's layer count. */
        uint32_t src_layers = r->srcSubresource.layerCount;
        if (src_layers == VK_REMAINING_ARRAY_LAYERS) {
            src_layers = src->create_info.arrayLayers - r->srcSubresource.baseArrayLayer;
        }
        uint32_t dst_layers = r->dstSubresource.layerCount;
        if (dst_layers == VK_REMAINING_ARRAY_LAYERS) {
            dst_layers = dst->create_info.arrayLayers - r->dstSubresource.baseArrayLayer;
        }
        uint32_t num_layers = (src_layers < dst_layers) ? src_layers : dst_layers;
        if (num_layers == 0) num_layers = 1;

        /* Slice pitch: for 3D textures, each depth slice is a full 2D image.
         * For array textures, each array layer is a full 2D image.
         * For cubemaps, each face is a full 2D image.
         * Memory layout: layer 0 depth 0, layer 0 depth 1, ..., layer 1 depth 0, ...
         * So the offset for (layer, z) is ((baseArrayLayer + layer) * depth + z) * slice_size. */
        uint64_t src_slice_size = (uint64_t)src_pitch * copy_height;
        uint64_t dst_slice_size = (uint64_t)dst_pitch * copy_height;

        for (uint32_t z = 0; z < r->extent.depth; z++) {
            for (uint32_t layer = 0; layer < num_layers; layer++) {
                uint64_t src_slice_off =
                    (uint64_t)((r->srcSubresource.baseArrayLayer + layer) * r->extent.depth + z) * src_slice_size;
                uint64_t dst_slice_off =
                    (uint64_t)((r->dstSubresource.baseArrayLayer + layer) * r->extent.depth + z) * dst_slice_size;
                for (uint32_t y = 0; y < copy_height; y++) {
                    if (src->memory && dst->memory &&
                        src->memory->gnm_mem.mapped && dst->memory->gnm_mem.mapped) {
                        /* For compressed formats, offsets are in blocks (4x4).
                         * srcOffset.x/y are in pixels, so divide by 4 for block offsets.
                         * Use floor division — the spec requires block-aligned offsets. */
                        uint32_t src_x_bytes = compressed
                            ? (r->srcOffset.x / 4) * bpp
                            : r->srcOffset.x * bpp;
                        uint32_t dst_x_bytes = compressed
                            ? (r->dstOffset.x / 4) * bpp
                            : r->dstOffset.x * bpp;
                        uint32_t src_y_pitch = compressed
                            ? ((r->srcOffset.y / 4) + y) * src_pitch
                            : (r->srcOffset.y + y) * src_pitch;
                        uint32_t dst_y_pitch = compressed
                            ? ((r->dstOffset.y / 4) + y) * dst_pitch
                            : (r->dstOffset.y + y) * dst_pitch;
                        uint64_t src_addr = (uint64_t)src->memory->gnm_mem.mapped +
                                            src->memory_offset +
                                            (uint64_t)src_y_pitch +
                                            (uint64_t)src_x_bytes +
                                            src_slice_off;
                        uint64_t dst_addr = (uint64_t)dst->memory->gnm_mem.mapped +
                                            dst->memory_offset +
                                            (uint64_t)dst_y_pitch +
                                            (uint64_t)dst_x_bytes +
                                            dst_slice_off;
                        sceGnmDrawCmdCopyMemory(&cmd->gnm_cmd, dst_addr, src_addr, copy_width);
                    }
                }
            }
        }
    }
    (void)srcImageLayout;
    (void)dstImageLayout;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                    VkImage dstImage, VkImageLayout dstImageLayout,
                    uint32_t regionCount, const VkImageBlit *pRegions, VkFilter filter) {
    /* BlitImage with scaling/format conversion requires a shader-based blit.
     * For MVP, fall back to CmdCopyImage if regions are 1:1.
     * Full blit support is Phase 3. */
    if (!commandBuffer || !pRegions) return;

    bool is_1to1 = true;
    for (uint32_t i = 0; i < regionCount; i++) {
        const VkImageBlit *r = &pRegions[i];
        if (r->srcOffsets[0].x != r->dstOffsets[0].x ||
            r->srcOffsets[0].y != r->dstOffsets[0].y ||
            r->srcOffsets[0].z != r->dstOffsets[0].z ||
            (r->srcOffsets[1].x - r->srcOffsets[0].x) != (r->dstOffsets[1].x - r->dstOffsets[0].x) ||
            (r->srcOffsets[1].y - r->srcOffsets[0].y) != (r->dstOffsets[1].y - r->dstOffsets[0].y) ||
            (r->srcOffsets[1].z - r->srcOffsets[0].z) != (r->dstOffsets[1].z - r->dstOffsets[0].z)) {
            is_1to1 = false;
            break;
        }
    }

    if (is_1to1) {
        /* Process in chunks of 16 to avoid stack overflow on large region counts */
        for (uint32_t chunk_start = 0; chunk_start < regionCount; chunk_start += 16) {
            VkImageCopy copies[16];
            uint32_t count = (regionCount - chunk_start > 16) ? 16 : (regionCount - chunk_start);
            for (uint32_t i = 0; i < count; i++) {
                const VkImageBlit *r = &pRegions[chunk_start + i];
                copies[i].srcSubresource = r->srcSubresource;
                copies[i].dstSubresource = r->dstSubresource;
                copies[i].srcOffset = r->srcOffsets[0];
                copies[i].dstOffset = r->dstOffsets[0];
                copies[i].extent.width = r->srcOffsets[1].x - r->srcOffsets[0].x;
                copies[i].extent.height = r->srcOffsets[1].y - r->srcOffsets[0].y;
                copies[i].extent.depth = r->srcOffsets[1].z - r->srcOffsets[0].z;
            }
            vk_ps4_CmdCopyImage(commandBuffer, srcImage, srcImageLayout,
                                dstImage, dstImageLayout, count, copies);
        }
    }
    (void)filter;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                       VkImage dstImage, VkImageLayout dstImageLayout,
                       uint32_t regionCount, const VkImageResolve *pRegions) {
    /* CmdResolveImage resolves a multisampled source image to a non-multisampled
     * destination image. Since we don't support MSAA yet (all images are 1-sample),
     * resolve is equivalent to a copy. */
    if (!commandBuffer || !pRegions) return;

    /* Process in chunks of 16 to avoid stack overflow on large region counts */
    for (uint32_t chunk_start = 0; chunk_start < regionCount; chunk_start += 16) {
        VkImageCopy copies[16];
        uint32_t count = (regionCount - chunk_start > 16) ? 16 : (regionCount - chunk_start);
        for (uint32_t i = 0; i < count; i++) {
            const VkImageResolve *r = &pRegions[chunk_start + i];
            copies[i].srcSubresource = r->srcSubresource;
            copies[i].dstSubresource = r->dstSubresource;
            copies[i].srcOffset = r->srcOffset;
            copies[i].dstOffset = r->dstOffset;
            copies[i].extent = r->extent;
        }
        vk_ps4_CmdCopyImage(commandBuffer, srcImage, srcImageLayout,
                            dstImage, dstImageLayout, count, copies);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
                            VkImageLayout dstImageLayout, uint32_t regionCount,
                            const VkBufferImageCopy *pRegions) {
    if (!commandBuffer || !pRegions) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_mark_operation(cmd, "CopyBufferToImage");
    vk_ps4_mark_operation(cmd, "ResolveImage");
    vk_ps4_mark_operation(cmd, "BlitImage");
    VkPs4Buffer *src = (VkPs4Buffer *)srcBuffer;
    VkPs4Image *dst = (VkPs4Image *)dstImage;
    if (!src || !dst || !src->memory || !dst->memory) return;
    if (!src->memory->gnm_mem.mapped || !dst->memory->gnm_mem.mapped) return;

    /* Bounded RGBA8 2D upload. Address calculation uses the same surface
     * description as the texture descriptor; the GPU reads the staging buffer
     * at execution time. Never read/upload its contents while recording. */
    if (dst->is_depth_target ||
        dst->create_info.imageType != VK_IMAGE_TYPE_2D ||
        dst->create_info.mipLevels != 1 || dst->create_info.arrayLayers != 1 ||
        dst->create_info.samples != VK_SAMPLE_COUNT_1_BIT ||
        dst->create_info.extent.width > 16384 || dst->create_info.extent.height > 16384 ||
        (dst->create_info.format != VK_FORMAT_R8G8B8A8_UNORM && dst->create_info.format != VK_FORMAT_R8G8B8A8_SRGB) ||
        !(src->create_info.usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) ||
        !(dst->create_info.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        (dstImageLayout != VK_IMAGE_LAYOUT_GENERAL && dstImageLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
        cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
    }
    GpaTextureInfo info = dst->is_render_target ? sceGnmRtBuildInfo(&dst->gnm_rt) : sceGnmTexBuildInfo(&dst->gnm_texture);
    GpaTilingParams tp = {0};
    GpaSurfaceContext context = {0};
    if (dst->memory_offset > dst->memory->size || src->memory_offset > src->memory->size ||
        src->create_info.size > src->memory->size - src->memory_offset) {
        cmd->resource_error = VK_ERROR_INITIALIZATION_FAILED; return;
    }
    uint64_t available = dst->memory->size - dst->memory_offset;
    if (sceGpaTpInit(&tp, &info, 0, 0) != GPA_ERR_OK ||
        sceGpaInitSurfaceContext(&context, available, &tp) != GPA_ERR_OK) {
        cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
    }
    for (uint32_t i = 0; i < regionCount; ++i) {
        const VkBufferImageCopy *r = &pRegions[i];
        uint32_t width = r->imageExtent.width, height = r->imageExtent.height;
        if (!width || !height || r->imageExtent.depth != 1 || r->imageOffset.z ||
            r->imageOffset.x < 0 || r->imageOffset.y < 0 ||
            r->imageSubresource.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
            r->imageSubresource.mipLevel || r->imageSubresource.baseArrayLayer ||
            r->imageSubresource.layerCount != 1 || (r->bufferOffset & 3) ||
            (uint64_t)r->imageOffset.x + width > dst->create_info.extent.width ||
            (uint64_t)r->imageOffset.y + height > dst->create_info.extent.height ||
            (r->bufferRowLength && r->bufferRowLength < width) ||
            (r->bufferImageHeight && r->bufferImageHeight < height)) {
            cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
        }
        uint64_t pitch = (uint64_t)(r->bufferRowLength ? r->bufferRowLength : width) * 4;
        uint64_t bytes = (height - 1) * pitch + (uint64_t)width * 4;
        if (r->bufferOffset > src->create_info.size || bytes > src->create_info.size - r->bufferOffset) {
            cmd->resource_error = VK_ERROR_INITIALIZATION_FAILED; return;
        }
        uint64_t srcbase = (uint64_t)src->memory->gnm_mem.mapped + src->memory_offset + r->bufferOffset;
        uint64_t dstbase = (uint64_t)dst->memory->gnm_mem.mapped + dst->memory_offset;
        for (uint32_t y = 0; y < height; ++y) {
            uint64_t run_offset = 0; uint32_t run_start = 0, run_size = 0;
            for (uint32_t x = 0; x <= width; ++x) {
                uint64_t offset = 0, bit = 0;
                if (x < width && (sceGpaComputeSurfaceCoord(&offset, &bit, &context,
                    r->imageOffset.x + x, r->imageOffset.y + y, 0, 0) != GPA_ERR_OK ||
                    bit || offset > available || available - offset < 4)) {
                    cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
                }
                if (run_size && (x == width || offset != run_offset + run_size)) {
                    if (!sceGnmDrawCmdCopyMemory(&cmd->gnm_cmd, dstbase + run_offset,
                        srcbase + y * pitch + (uint64_t)run_start * 4, run_size)) {
                        cmd->resource_error = VK_ERROR_OUT_OF_DEVICE_MEMORY; return;
                    }
                    run_size = 0;
                }
                if (x < width) {
                    if (!run_size) { run_offset = offset; run_start = x; }
                    run_size += 4;
                }
            }
        }
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                            VkBuffer dstBuffer, uint32_t regionCount,
                            const VkBufferImageCopy *pRegions) {
    if (!commandBuffer || !pRegions) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_mark_operation(cmd, "CopyImageToBuffer");
    VkPs4Buffer *buffer = (VkPs4Buffer *)dstBuffer;
    VkPs4Image *image = (VkPs4Image *)srcImage;
    if (!buffer || !image || !buffer->memory || !image->memory) return;
    if (!buffer->memory->gnm_mem.mapped || !image->memory->gnm_mem.mapped) return;

    /* Bounded RGBA8 readback: detile on the GPU into a pitched buffer.
     * Address calculation shares the render-target/texture surface layout. */
    if (image->is_depth_target ||
        image->create_info.imageType != VK_IMAGE_TYPE_2D ||
        image->create_info.mipLevels != 1 || image->create_info.arrayLayers != 1 ||
        image->create_info.samples != VK_SAMPLE_COUNT_1_BIT ||
        image->create_info.extent.width > 16384 || image->create_info.extent.height > 16384 ||
        (image->create_info.format != VK_FORMAT_R8G8B8A8_UNORM && image->create_info.format != VK_FORMAT_R8G8B8A8_SRGB) ||
        !(buffer->create_info.usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) ||
        !(image->create_info.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
        (srcImageLayout != VK_IMAGE_LAYOUT_GENERAL && srcImageLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)) {
        cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
    }
    GpaTextureInfo info = image->is_render_target ? sceGnmRtBuildInfo(&image->gnm_rt) : sceGnmTexBuildInfo(&image->gnm_texture);
    GpaTilingParams tp = {0};
    GpaSurfaceContext context = {0};
    if (image->memory_offset > image->memory->size ||
        buffer->memory_offset > buffer->memory->size ||
        buffer->create_info.size > buffer->memory->size - buffer->memory_offset) {
        cmd->resource_error = VK_ERROR_INITIALIZATION_FAILED; return;
    }
    uint64_t available = image->memory->size - image->memory_offset;
    if (sceGpaTpInit(&tp, &info, 0, 0) != GPA_ERR_OK ||
        sceGpaInitSurfaceContext(&context, available, &tp) != GPA_ERR_OK) {
        cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
    }
    /* Snapshot the complete backing surface once, then detile from a buffer.
     * This also avoids treating individual tiled spans as image aliases. */
    uint64_t surface_size = 0; uint32_t alignment = 0;
    GnmError err = image->is_render_target ?
        sceGnmRtCalcByteSize(&surface_size, &alignment, &image->gnm_rt) :
        sceGnmTexCalcByteSize(&surface_size, &alignment, &image->gnm_texture);
    if (err != GNM_ERROR_OK || !surface_size || surface_size > 16 * 1024 * 1024 ||
        surface_size > available || (surface_size & 3)) {
        cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
    }
    if (cmd->readback_count == VK_PS4_MAX_READBACK_SCRATCH) {
        cmd->resource_error = VK_ERROR_OUT_OF_DEVICE_MEMORY; return;
    }
    GnmDirectMemory *scratch = &cmd->readback_scratch[cmd->readback_count++];
    /* A slot is used once per recording; reset/re-record requires completed GPU use. */
    if (scratch->allocated && scratch->size < surface_size) sceGnmDirectMemoryRelease(scratch);
    if (!scratch->allocated && sceGnmDirectMemoryAllocate(
        scratch, surface_size, 65536, GNM_DIRECT_MEMORY_TYPE_WC_GARLIC,
        GNM_PROT_CPU_GPU_RW) != GNM_ERROR_OK) {
        cmd->resource_error = VK_ERROR_OUT_OF_DEVICE_MEMORY; return;
    }
    if (!sceGnmDrawCmdCopyMemory(&cmd->gnm_cmd, (uint64_t)scratch->mapped,
        (uint64_t)image->memory->gnm_mem.mapped + image->memory_offset, (uint32_t)surface_size)) {
        cmd->resource_error = VK_ERROR_OUT_OF_DEVICE_MEMORY; return;
    }
    for (uint32_t i = 0; i < regionCount; ++i) {
        const VkBufferImageCopy *r = &pRegions[i];
        uint32_t width = r->imageExtent.width, height = r->imageExtent.height;
        if (!width || !height || r->imageExtent.depth != 1 || r->imageOffset.z ||
            r->imageOffset.x < 0 || r->imageOffset.y < 0 ||
            r->imageSubresource.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
            r->imageSubresource.mipLevel || r->imageSubresource.baseArrayLayer ||
            r->imageSubresource.layerCount != 1 || (r->bufferOffset & 3) ||
            (uint64_t)r->imageOffset.x + width > image->create_info.extent.width ||
            (uint64_t)r->imageOffset.y + height > image->create_info.extent.height ||
            (r->bufferRowLength && r->bufferRowLength < width) ||
            (r->bufferImageHeight && r->bufferImageHeight < height)) {
            cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
        }
        uint64_t pitch = (uint64_t)(r->bufferRowLength ? r->bufferRowLength : width) * 4;
        uint64_t bytes = (height - 1) * pitch + (uint64_t)width * 4;
        if (r->bufferOffset > buffer->create_info.size || bytes > buffer->create_info.size - r->bufferOffset) {
            cmd->resource_error = VK_ERROR_INITIALIZATION_FAILED; return;
        }
        uint64_t buffer_base = (uint64_t)buffer->memory->gnm_mem.mapped + buffer->memory_offset + r->bufferOffset;
        uint64_t image_base = (uint64_t)scratch->mapped;
        for (uint32_t y = 0; y < height; ++y) {
            uint64_t run_offset = 0; uint32_t run_start = 0, run_size = 0;
            for (uint32_t x = 0; x <= width; ++x) {
                uint64_t offset = 0, bit = 0;
                if (x < width && (sceGpaComputeSurfaceCoord(&offset, &bit, &context,
                    r->imageOffset.x + x, r->imageOffset.y + y, 0, 0) != GPA_ERR_OK ||
                    bit || offset > surface_size || surface_size - offset < 4)) {
                    cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
                }
                if (run_size && (x == width || offset != run_offset + run_size)) {
                    if (!sceGnmDrawCmdCopyMemory(&cmd->gnm_cmd, buffer_base + y * pitch + (uint64_t)run_start * 4,
                        image_base + run_offset, run_size)) {
                        cmd->resource_error = VK_ERROR_OUT_OF_DEVICE_MEMORY; return;
                    }
                    run_size = 0;
                }
                if (x < width) {
                    if (!run_size) { run_offset = offset; run_start = x; }
                    run_size += 4;
                }
            }
        }
    }
}


/* === Render pass commands === */

/* Check if attachment `att_idx` is used by subpass `subpass` (via any
 * color, depth/stencil, input, or resolve attachment reference). */
static bool vk_ps4_attachment_used_in_subpass(VkPs4RenderPass *rp,
                                               uint32_t att_idx, uint32_t subpass) {
    if (subpass >= rp->subpass_count) return false;
    const VkSubpassDescription *sp = &rp->subpasses[subpass];

    if (sp->pColorAttachments) {
        for (uint32_t i = 0; i < sp->colorAttachmentCount; i++) {
            if (sp->pColorAttachments[i].attachment == att_idx) return true;
        }
    }
    if (sp->pDepthStencilAttachment &&
        sp->pDepthStencilAttachment->attachment == att_idx) {
        return true;
    }
    if (sp->pInputAttachments) {
        for (uint32_t i = 0; i < sp->inputAttachmentCount; i++) {
            if (sp->pInputAttachments[i].attachment == att_idx) return true;
        }
    }
    if (sp->pResolveAttachments) {
        for (uint32_t i = 0; i < sp->colorAttachmentCount; i++) {
            if (sp->pResolveAttachments[i].attachment == att_idx) return true;
        }
    }
    return false;
}

/* Check if attachment `att_idx` is used in any subpass before `subpass`. */
static bool vk_ps4_attachment_used_before_subpass(VkPs4RenderPass *rp,
                                                   uint32_t att_idx, uint32_t subpass) {
    for (uint32_t s = 0; s < subpass; s++) {
        if (vk_ps4_attachment_used_in_subpass(rp, att_idx, s)) return true;
    }
    return false;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pBeginInfo,
                          VkSubpassContents contents) {
    (void)contents;
    if (!commandBuffer || !pBeginInfo) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_mark_operation(cmd, "BeginRenderPass");
    VkPs4RenderPass *rp = (VkPs4RenderPass *)pBeginInfo->renderPass;
    VkPs4Framebuffer *fb = (VkPs4Framebuffer *)pBeginInfo->framebuffer;

    if (!rp || !fb) return;

    cmd->current_render_pass.pass = rp;
    cmd->current_render_pass.framebuffer = fb;
    cmd->current_render_pass.render_area = pBeginInfo->renderArea;
    cmd->current_render_pass.current_subpass = 0;

    /* For imageless framebuffers, extract attachment views from
     * VkRenderPassAttachmentBeginInfo in the pNext chain. */
    cmd->current_render_pass.imageless_attachment_count = 0;
    if (fb->imageless) {
        VkBaseInStructure *chain = (VkBaseInStructure *)pBeginInfo->pNext;
        while (chain) {
            if (chain->sType == VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO) {
                VkRenderPassAttachmentBeginInfo *att_begin =
                    (VkRenderPassAttachmentBeginInfo *)chain;
                uint32_t count = att_begin->attachmentCount;
                if (count > 16) count = 16;
                for (uint32_t i = 0; i < count; i++) {
                    cmd->current_render_pass.imageless_attachments[i] =
                        (VkPs4ImageView *)att_begin->pAttachments[i];
                }
                cmd->current_render_pass.imageless_attachment_count = count;
                break;
            }
            chain = (VkBaseInStructure *)chain->pNext;
        }
    }

    /* Deep-copy clear values — the caller's pClearValues may be freed
     * after CmdBeginRenderPass returns, but CmdNextSubpass may need
     * them later for attachments first used in subsequent subpasses. */
    cmd->current_render_pass.clear_value_count =
        (pBeginInfo->clearValueCount > 16) ? 16 : pBeginInfo->clearValueCount;
    if (pBeginInfo->pClearValues && cmd->current_render_pass.clear_value_count > 0) {
        memcpy(cmd->current_render_pass.clear_values,
               pBeginInfo->pClearValues,
               cmd->current_render_pass.clear_value_count * sizeof(VkClearValue));
    }

    /* Set scissor to render area first (needed for draw-based clears) */
    sceGnmDrawCmdSetScreenScissor(&cmd->gnm_cmd,
        pBeginInfo->renderArea.offset.x,
        pBeginInfo->renderArea.offset.y,
        pBeginInfo->renderArea.offset.x + pBeginInfo->renderArea.extent.width,
        pBeginInfo->renderArea.offset.y + pBeginInfo->renderArea.extent.height);

    /* Bind render targets based on the current subpass description.
     * This correctly maps framebuffer attachments to RT slots via
     * pColorAttachments[j].attachment, and binds the depth/stencil
     * attachment via pDepthStencilAttachment. */
    vk_ps4_bind_subpass_targets(cmd);

    /* If any framebuffer attachment is a swapchain image, emit
     * WaitUntilSafeForRendering so the GPU waits until the display
     * engine has finished reading the buffer before we render to it.
     * This prevents tearing and GPU/display races on swapchain images. */
    for (uint32_t i = 0; i < fb->attachment_count; i++) {
        VkPs4ImageView *view = vk_ps4_get_attachment_view(cmd, i);
        if (view && view->image && view->image->is_swapchain_image) {
            sceGnmDrawCmdWaitUntilSafeForRendering(
                &cmd->gnm_cmd,
                view->image->video_out_handle,
                view->image->swapchain_buffer_index
            );
        }
    }

    /* Clear attachments whose loadOp is CLEAR and that are first used
     * in subpass 0. Per the Vulkan spec, the load operation for each
     * attachment is performed at the beginning of the subpass in which
     * the attachment is first used. Attachments first used in later
     * subpasses are cleared in CmdNextSubpass. */
    if (cmd->current_render_pass.clear_value_count > 0) {
        for (uint32_t i = 0; i < cmd->current_render_pass.clear_value_count && i < fb->attachment_count; i++) {
            VkPs4ImageView *view = vk_ps4_get_attachment_view(cmd, i);
            if (!view || !view->image) continue;

            if (i >= rp->attachment_count) continue;
            VkAttachmentLoadOp load_op = rp->attachments[i].loadOp;
            if (load_op != VK_ATTACHMENT_LOAD_OP_CLEAR) continue;

            /* Only clear if this attachment is first used in subpass 0 */
            if (!vk_ps4_attachment_used_in_subpass(rp, i, 0)) continue;
            /* If it was used before subpass 0 (impossible, but guard anyway) */
            if (vk_ps4_attachment_used_before_subpass(rp, i, 0)) continue;

            if (view->image->is_depth_target) {
                /* Depth/stencil clear — only enable clear bits for planes
                 * that exist in the attachment format. */
                const VkClearDepthStencilValue *ds =
                    &cmd->current_render_pass.clear_values[i].depthStencil;
                VkFormat ds_fmt = view->image->create_info.format;
                bool has_depth = vk_format_has_depth(ds_fmt);
                bool has_stencil = vk_format_has_stencil(ds_fmt);

                if (has_depth) {
                    sceGnmDrawCmdSetDepthClearValue(&cmd->gnm_cmd, ds->depth);
                }
                if (has_stencil) {
                    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                        R_028028_DB_STENCIL_CLEAR, S_028028_CLEAR(ds->stencil));
                }

                /* Enable only the relevant clear bits in DB_RENDER_CONTROL */
                GnmDbRenderControl db_ctrl;
                memset(&db_ctrl, 0, sizeof(db_ctrl));
                db_ctrl.depthclearenable = has_depth ? 1 : 0;
                db_ctrl.stencilclearenable = has_stencil ? 1 : 0;
                sceGnmDrawCmdSetDbRenderControl(&cmd->gnm_cmd, &db_ctrl);

                /* Trigger the lazy clear with a fullscreen draw. */
                vk_ps4_clear_depth_draw(&cmd->gnm_cmd, true);

            } else if (view->image->is_render_target) {
                const VkClearColorValue *cc =
                    &cmd->current_render_pass.clear_values[i].color;
                vk_ps4_clear_color(cmd, view->image, cc);
            }
        }
    }

    /* Re-bind subpass targets after load-op clears.  Draw-based clears
     * (vk_ps4_clear_color_draw) bind the cleared RT at slot 0 and clobber
     * blend/scissor state.  Re-binding restores the correct RT-to-slot
     * mapping from the subpass description.  Pipeline state (PS, VS,
     * blend, scissor) is restored when the user calls CmdBindPipeline. */
    vk_ps4_bind_subpass_targets(cmd);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdEndRenderPass(VkCommandBuffer commandBuffer) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_mark_operation(cmd, "EndRenderPass");

    /* Reset DB_RENDER_CONTROL clear flags.
     * The lazy clear is triggered immediately by vk_ps4_clear_depth_draw()
     * in CmdBeginRenderPass/CmdClearAttachments, so by the time we reach
     * CmdEndRenderPass, the clear has already fired. Resetting the bits
     * here prevents stale clear flags from persisting into the next
     * render pass (which could cause an unexpected clear on the first
     * depth-accessing draw). */
    GnmDbRenderControl db_ctrl;
    memset(&db_ctrl, 0, sizeof(db_ctrl));
    db_ctrl.depthclearenable = 0;
    db_ctrl.stencilclearenable = 0;
    sceGnmDrawCmdSetDbRenderControl(&cmd->gnm_cmd, &db_ctrl);

    /* Emit EOP event to signal completion */
    sceGnmDrawCmdEventWriteEop(&cmd->gnm_cmd,
        GNM_CACHE_FLUSH_AND_INV_TS_EVENT, 0,
        GNM_DATA_SEL_DISCARD, 0);

    cmd->current_render_pass.pass = NULL;
    cmd->current_render_pass.framebuffer = NULL;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents) {
    (void)contents;
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* Advance to the next subpass and re-bind render targets.
     * Bounds check: if we're already at the last subpass, do nothing. */
    if (!cmd->current_render_pass.pass) return;
    VkPs4RenderPass *rp = cmd->current_render_pass.pass;
    VkPs4Framebuffer *fb = cmd->current_render_pass.framebuffer;
    uint32_t next = cmd->current_render_pass.current_subpass + 1;
    if (next >= rp->subpass_count) return;

    cmd->current_render_pass.current_subpass = next;
    vk_ps4_bind_subpass_targets(cmd);

    /* Perform load-op clears for attachments first used in this subpass.
     * An attachment is "first used" if it appears in this subpass but not
     * in any previous subpass. We use the helper that checks all attachment
     * reference types (color, depth/stencil, input, resolve). */
    if (fb && cmd->current_render_pass.render_area.extent.width > 0) {
        const VkSubpassDescription *subpass = &rp->subpasses[next];

        /* Collect all attachment indices used by this subpass */
        for (uint32_t j = 0; j < subpass->colorAttachmentCount; j++) {
            if (subpass->pColorAttachments) {
                uint32_t att_idx = subpass->pColorAttachments[j].attachment;
                if (att_idx == VK_ATTACHMENT_UNUSED) continue;
                if (att_idx >= rp->attachment_count || att_idx >= fb->attachment_count)
                    continue;
                if (rp->attachments[att_idx].loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR) continue;
                if (vk_ps4_attachment_used_before_subpass(rp, att_idx, next)) continue;

                /* First use — clear it */
                VkPs4ImageView *view = vk_ps4_get_attachment_view(cmd, att_idx);
                if (!view || !view->image || !view->image->is_render_target) continue;
                if (att_idx < cmd->current_render_pass.clear_value_count) {
                    const VkClearColorValue *cc =
                        &cmd->current_render_pass.clear_values[att_idx].color;
                    vk_ps4_clear_color(cmd, view->image, cc);
                }
            }
            /* Also check resolve attachments */
            if (subpass->pResolveAttachments) {
                uint32_t att_idx = subpass->pResolveAttachments[j].attachment;
                if (att_idx == VK_ATTACHMENT_UNUSED) continue;
                if (att_idx >= rp->attachment_count || att_idx >= fb->attachment_count)
                    continue;
                if (rp->attachments[att_idx].loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR) continue;
                if (vk_ps4_attachment_used_before_subpass(rp, att_idx, next)) continue;

                VkPs4ImageView *view = vk_ps4_get_attachment_view(cmd, att_idx);
                if (!view || !view->image || !view->image->is_render_target) continue;
                if (att_idx < cmd->current_render_pass.clear_value_count) {
                    const VkClearColorValue *cc =
                        &cmd->current_render_pass.clear_values[att_idx].color;
                    vk_ps4_clear_color(cmd, view->image, cc);
                }
            }
        }

        /* Check input attachments */
        if (subpass->pInputAttachments) {
            for (uint32_t j = 0; j < subpass->inputAttachmentCount; j++) {
                uint32_t att_idx = subpass->pInputAttachments[j].attachment;
                if (att_idx == VK_ATTACHMENT_UNUSED) continue;
                if (att_idx >= rp->attachment_count || att_idx >= fb->attachment_count)
                    continue;
                if (rp->attachments[att_idx].loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR) continue;
                if (vk_ps4_attachment_used_before_subpass(rp, att_idx, next)) continue;

                VkPs4ImageView *view = vk_ps4_get_attachment_view(cmd, att_idx);
                if (!view || !view->image) continue;
                if (view->image->is_render_target) {
                    if (att_idx < cmd->current_render_pass.clear_value_count) {
                        const VkClearColorValue *cc =
                            &cmd->current_render_pass.clear_values[att_idx].color;
                        vk_ps4_clear_color(cmd, view->image, cc);
                    }
                } else if (view->image->is_depth_target) {
                    if (att_idx < cmd->current_render_pass.clear_value_count) {
                        const VkClearDepthStencilValue *ds =
                            &cmd->current_render_pass.clear_values[att_idx].depthStencil;
                        VkFormat ds_fmt = view->image->create_info.format;
                        bool has_depth = vk_format_has_depth(ds_fmt);
                        bool has_stencil = vk_format_has_stencil(ds_fmt);
                        if (has_depth)
                            sceGnmDrawCmdSetDepthClearValue(&cmd->gnm_cmd, ds->depth);
                        if (has_stencil)
                            vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                                R_028028_DB_STENCIL_CLEAR, S_028028_CLEAR(ds->stencil));
                        GnmDbRenderControl db_ctrl;
                        memset(&db_ctrl, 0, sizeof(db_ctrl));
                        db_ctrl.depthclearenable = has_depth ? 1 : 0;
                        db_ctrl.stencilclearenable = has_stencil ? 1 : 0;
                        sceGnmDrawCmdSetDbRenderControl(&cmd->gnm_cmd, &db_ctrl);
                        vk_ps4_clear_depth_draw(&cmd->gnm_cmd, true);
                    }
                }
            }
        }

        /* Check depth/stencil attachment */
        if (subpass->pDepthStencilAttachment) {
            uint32_t att_idx = subpass->pDepthStencilAttachment->attachment;
            if (att_idx != VK_ATTACHMENT_UNUSED &&
                att_idx < rp->attachment_count && att_idx < fb->attachment_count &&
                rp->attachments[att_idx].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR &&
                !vk_ps4_attachment_used_before_subpass(rp, att_idx, next)) {

                VkPs4ImageView *view = vk_ps4_get_attachment_view(cmd, att_idx);
                if (view && view->image && view->image->is_depth_target) {
                    if (att_idx < cmd->current_render_pass.clear_value_count) {
                        const VkClearDepthStencilValue *ds =
                            &cmd->current_render_pass.clear_values[att_idx].depthStencil;
                        VkFormat ds_fmt = view->image->create_info.format;
                        bool has_depth = vk_format_has_depth(ds_fmt);
                        bool has_stencil = vk_format_has_stencil(ds_fmt);
                        if (has_depth)
                            sceGnmDrawCmdSetDepthClearValue(&cmd->gnm_cmd, ds->depth);
                        if (has_stencil)
                            vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                                R_028028_DB_STENCIL_CLEAR, S_028028_CLEAR(ds->stencil));
                        GnmDbRenderControl db_ctrl;
                        memset(&db_ctrl, 0, sizeof(db_ctrl));
                        db_ctrl.depthclearenable = has_depth ? 1 : 0;
                        db_ctrl.stencilclearenable = has_stencil ? 1 : 0;
                        sceGnmDrawCmdSetDbRenderControl(&cmd->gnm_cmd, &db_ctrl);
                        vk_ps4_clear_depth_draw(&cmd->gnm_cmd, true);
                    }
                }
            }
        }
    }

    /* Re-bind subpass targets after load-op clears.  Draw-based clears
     * bind the cleared RT at slot 0 and clobber blend/scissor state.
     * Re-binding restores the correct RT-to-slot mapping. */
    vk_ps4_bind_subpass_targets(cmd);
}

/* === Barriers === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
                          VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
                          uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
                          uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                          uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_mark_operation(cmd, "PipelineBarrier");

    /* Conservative whole-pipeline synchronization. A per-command-buffer
     * label prevents interference between independently recorded buffers.
     * Reset it on the GPU on every execution so resubmission cannot observe
     * a stale completion value. This does not implement queue ownership. */
    for (uint32_t i = 0; i < imageMemoryBarrierCount; ++i) {
        const VkImageMemoryBarrier *b = &pImageMemoryBarriers[i];
        if (b->srcQueueFamilyIndex != b->dstQueueFamilyIndex) {
            cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
        }
    }
    for (uint32_t i = 0; i < bufferMemoryBarrierCount; ++i) {
        const VkBufferMemoryBarrier *b = &pBufferMemoryBarriers[i];
        if (b->srcQueueFamilyIndex != b->dstQueueFamilyIndex) {
            cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
        }
    }
    if (!cmd->barrier_label.allocated &&
        sceGnmDirectMemoryAllocate(&cmd->barrier_label, 4, 65536,
            GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW) != GNM_ERROR_OK) {
        cmd->resource_error = VK_ERROR_OUT_OF_DEVICE_MEMORY; return;
    }
    /* Fill(7), EOP(6), WAIT_REG_MEM(7), ACQUIRE_MEM(7). */
    if ((uint32_t)(cmd->gnm_cmd.cmdptr - cmd->gnm_cmd.beginptr) + 32 > cmd->pm4_buffer_size) {
        cmd->resource_error = VK_ERROR_OUT_OF_DEVICE_MEMORY; return;
    }
    uint64_t label = (uint64_t)cmd->barrier_label.mapped;
    sceGnmDrawCmdFillMemory(&cmd->gnm_cmd, label, 4, 0);
    sceGnmDrawCmdEventWriteEop(&cmd->gnm_cmd, GNM_CACHE_FLUSH_AND_INV_TS_EVENT,
        label, GNM_DATA_SEL_SEND_DATA32, 1);
    cmd->gnm_cmd.cmdptr[-3] &= ~(3u << 24); /* Poll instead of CP interrupt. */
    sceGnmDrawCmdWaitMem(&cmd->gnm_cmd, GNM_WAIT_REG_MEM_FUNC_EQUAL, label, 1, 0xffffffff);
    sceGnmDrawCmdWaitGraphicsWrite(&cmd->gnm_cmd, 0);
    /* Layout is a command execution contract. Do not mutate global image
     * state while recording buffers that may be submitted in another order. */

    (void)srcStageMask;
    (void)dstStageMask;
    (void)dependencyFlags;
    (void)memoryBarrierCount;
    (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount;
    (void)pBufferMemoryBarriers;
}

/* === Event commands === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) {
    if (!commandBuffer || !event) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Event *ev = (VkPs4Event *)event;

    /* Emit an EOP event write to signal the GPU side.
     * The CPU-side signaled flag is also set for GetEventStatus polling.
     * KNOWN LIMITATION: Without a GPU-visible memory location for the event,
     * we can't truly signal a GPU event. This sets the CPU flag and emits
     * a cache flush as a side effect. */
    sceGnmDrawCmdEventWriteEop(&cmd->gnm_cmd,
        GNM_CACHE_FLUSH_AND_INV_TS_EVENT, 0,
        GNM_DATA_SEL_DISCARD, 0);
    ev->signaled = true;
    (void)stageMask;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) {
    if (!commandBuffer || !event) return;
    VkPs4Event *ev = (VkPs4Event *)event;

    /* Reset the CPU-side flag. On the GPU side, there's nothing to do
     * since we don't have a GPU-visible event memory location. */
    ev->signaled = false;
    (void)stageMask;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent *pEvents,
                     VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                     uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
                     uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                     uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* Emit a cache flush + wait as a full barrier.
     * KNOWN LIMITATION: Without GPU-visible event memory, we can't wait
     * on specific events. We emit a full pipeline stall instead, which
     * is over-synchronized but safe. */
    sceGnmDrawCmdEventWriteEop(&cmd->gnm_cmd,
        GNM_CACHE_FLUSH_AND_INV_TS_EVENT, 0,
        GNM_DATA_SEL_DISCARD, 0);
    sceGnmDrawCmdWaitGraphicsWrite(&cmd->gnm_cmd, 0);

    /* Track image layout transitions */
    for (uint32_t i = 0; i < imageMemoryBarrierCount; i++) {
        const VkImageMemoryBarrier *b = &pImageMemoryBarriers[i];
        if (b->oldLayout != b->newLayout) {
            VkPs4Image *img = (VkPs4Image *)b->image;
            if (img) img->layout = b->newLayout;
        }
    }

    (void)eventCount; (void)pEvents;
    (void)srcStageMask; (void)dstStageMask;
    (void)memoryBarrierCount; (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount; (void)pBufferMemoryBarriers;
}

/* === Clear commands === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
                          const VkClearColorValue *pColor, uint32_t rangeCount,
                          const VkImageSubresourceRange *pRanges) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_mark_operation(cmd, "ClearColorImage");
    VkPs4Image *img = (VkPs4Image *)image;
    if (!img || !pColor || !pRanges || rangeCount != 1 ||
        !img->memory || !img->memory->gnm_mem.mapped || img->is_depth_target ||
        img->create_info.imageType != VK_IMAGE_TYPE_2D ||
        img->create_info.format != VK_FORMAT_R8G8B8A8_UNORM ||
        img->create_info.mipLevels != 1 || img->create_info.arrayLayers != 1 ||
        img->create_info.samples != VK_SAMPLE_COUNT_1_BIT ||
        !(img->create_info.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        (imageLayout != VK_IMAGE_LAYOUT_GENERAL && imageLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) ||
        pRanges[0].aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
        pRanges[0].baseMipLevel || pRanges[0].baseArrayLayer ||
        (pRanges[0].levelCount != 1 && pRanges[0].levelCount != VK_REMAINING_MIP_LEVELS) ||
        (pRanges[0].layerCount != 1 && pRanges[0].layerCount != VK_REMAINING_ARRAY_LAYERS)) {
        cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
    }
    uint64_t size = 0; uint32_t align = 0;
    GnmError err = img->is_render_target ?
        sceGnmRtCalcByteSize(&size, &align, &img->gnm_rt) :
        sceGnmTexCalcByteSize(&size, &align, &img->gnm_texture);
    if (err != GNM_ERROR_OK || !size || size > 16 * 1024 * 1024 || (size & 3) ||
        img->memory_offset > img->memory->size || size > img->memory->size - img->memory_offset) {
        cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
    }
    uint32_t packed = 0;
    for (unsigned c = 0; c < 4; ++c) {
        float value = pColor->float32[c];
        /* Clamp before conversion, including NaN, to avoid undefined float casts. */
        if (!(value > 0)) value = 0;
        if (value > 1) value = 1;
        packed |= (uint32_t)(value * 255.f + .5f) << (c * 8);
    }
    /* Stage the constant surface, then copy into the image. In shadPS4's
     * buffer/image alias path a direct fill does not invalidate a cached image. */
    if (cmd->readback_count == VK_PS4_MAX_READBACK_SCRATCH) {
        cmd->resource_error = VK_ERROR_OUT_OF_DEVICE_MEMORY; return;
    }
    GnmDirectMemory *scratch = &cmd->readback_scratch[cmd->readback_count++];
    if (scratch->allocated && scratch->size < size) sceGnmDirectMemoryRelease(scratch);
    if (!scratch->allocated && sceGnmDirectMemoryAllocate(scratch, size, 65536,
        GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW) != GNM_ERROR_OK) {
        cmd->resource_error = VK_ERROR_OUT_OF_DEVICE_MEMORY; return;
    }
    if (!sceGnmDrawCmdFillMemory(&cmd->gnm_cmd, (uint64_t)scratch->mapped, (uint32_t)size, packed) ||
        !sceGnmDrawCmdCopyMemory(&cmd->gnm_cmd,
            (uint64_t)img->memory->gnm_mem.mapped + img->memory_offset,
            (uint64_t)scratch->mapped, (uint32_t)size))
        cmd->resource_error = VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
                                 const VkClearDepthStencilValue *pDepthStencil, uint32_t rangeCount,
                                 const VkImageSubresourceRange *pRanges) {
    if (!commandBuffer || !image || !pDepthStencil || !pRanges) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_mark_operation(cmd, "ClearDepthStencilImage");
    VkPs4Image *img = (VkPs4Image *)image;
    if (!img->is_depth_target) return;

    VkFormat ds_fmt = img->create_info.format;
    bool has_depth = vk_format_has_depth(ds_fmt);
    bool has_stencil = vk_format_has_stencil(ds_fmt);

    /* Set depth + stencil clear values and enable clear in DB_RENDER_CONTROL. */
    if (has_depth) {
        sceGnmDrawCmdSetDepthClearValue(&cmd->gnm_cmd, pDepthStencil->depth);
    }
    if (has_stencil) {
        vk_ps4_emit_context_reg(&cmd->gnm_cmd,
            R_028028_DB_STENCIL_CLEAR, S_028028_CLEAR(pDepthStencil->stencil));
    }

    GnmDbRenderControl db_ctrl;
    memset(&db_ctrl, 0, sizeof(db_ctrl));
    db_ctrl.depthclearenable = has_depth ? 1 : 0;
    db_ctrl.stencilclearenable = has_stencil ? 1 : 0;
    sceGnmDrawCmdSetDbRenderControl(&cmd->gnm_cmd, &db_ctrl);

    /* Bind the depth target and draw to trigger the lazy clear. */
    sceGnmDrawCmdSetDepthRenderTarget(&cmd->gnm_cmd, &img->gnm_drt);
    sceGnmDrawCmdSetScreenScissor(&cmd->gnm_cmd,
        0, 0,
        img->create_info.extent.width,
        img->create_info.extent.height);
    vk_ps4_clear_depth_draw(&cmd->gnm_cmd, true);

    /* Iterate ranges to validate bounds (API contract). The lazy clear
     * covers the entire target regardless of ranges. */
    for (uint32_t r = 0; r < rangeCount; r++) {
        const VkImageSubresourceRange *range = &pRanges[r];
        uint32_t max_mip = img->create_info.mipLevels;
        if (range->baseMipLevel >= max_mip) continue;
        uint32_t max_layers = img->create_info.arrayLayers;
        uint32_t layer_count = range->layerCount == VK_REMAINING_ARRAY_LAYERS
            ? max_layers - range->baseArrayLayer
            : range->layerCount;
        if (range->baseArrayLayer + layer_count > max_layers) continue;
    }
    (void)imageLayout;
}

/* Color attachment meta-clear. Preserve unselected tiled pixels and draw state. */
VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                           const VkClearAttachment *pAttachments, uint32_t rectCount,
                           const VkClearRect *pRects) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_mark_operation(cmd, "ClearAttachments");
    if (!attachmentCount || !rectCount) return;
    if (!pAttachments || !pRects || !cmd->current_render_pass.pass ||
        !cmd->current_render_pass.framebuffer) goto unsupported;
    for (uint32_t a = 0; a < attachmentCount; ++a) {
        const VkClearAttachment *att = &pAttachments[a];
        if (att->aspectMask && !(att->aspectMask & ~(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT))) {
            uint32_t idx = vk_ps4_subpass_depth_attachment(cmd);
            VkPs4ImageView *view = vk_ps4_get_attachment_view(cmd, idx);
            if (!view || !view->image || !view->image->is_depth_target) goto unsupported;
            VkPs4Image *img = view->image;
            for (uint32_t r=0;r<rectCount;++r) {
                const VkRect2D *rect=&pRects[r].rect;
                if (pRects[r].baseArrayLayer || pRects[r].layerCount!=1 || rect->offset.x<0 || rect->offset.y<0 ||
                    (uint64_t)rect->offset.x+rect->extent.width>img->create_info.extent.width ||
                    (uint64_t)rect->offset.y+rect->extent.height>img->create_info.extent.height) goto unsupported;
                sceGnmDrawCmdSetDepthRenderTarget(&cmd->gnm_cmd,&img->gnm_drt);
                sceGnmDrawCmdSetDepthClearValue(&cmd->gnm_cmd,att->clearValue.depthStencil.depth);
                vk_ps4_emit_context_reg(&cmd->gnm_cmd,R_028028_DB_STENCIL_CLEAR,S_028028_CLEAR(att->clearValue.depthStencil.stencil));
                GnmDbRenderControl ctrl={0};
                ctrl.depthclearenable=!!(att->aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT);
                ctrl.stencilclearenable=!!(att->aspectMask&VK_IMAGE_ASPECT_STENCIL_BIT);
                sceGnmDrawCmdSetDbRenderControl(&cmd->gnm_cmd,&ctrl);
                sceGnmDrawCmdSetScreenScissor(&cmd->gnm_cmd,rect->offset.x,rect->offset.y,rect->offset.x+rect->extent.width,rect->offset.y+rect->extent.height);
                sceGnmDrawCmdSetRenderTargetMask(&cmd->gnm_cmd,0);
                vk_ps4_clear_depth_draw(&cmd->gnm_cmd, ctrl.depthclearenable);
                memset(&ctrl,0,sizeof(ctrl)); sceGnmDrawCmdSetDbRenderControl(&cmd->gnm_cmd,&ctrl);
            }
            if (cmd->current_pipeline) vk_ps4_CmdBindPipeline(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,(VkPipeline)cmd->current_pipeline);
            continue;
        }
        if (att->aspectMask != VK_IMAGE_ASPECT_COLOR_BIT) goto unsupported;
        uint32_t idx = vk_ps4_subpass_color_attachment(cmd, att->colorAttachment);
        if (idx == VK_ATTACHMENT_UNUSED) continue;
        VkPs4Framebuffer *fb = cmd->current_render_pass.framebuffer;
        if (fb->imageless || !fb->attachments || idx >= fb->attachment_count) goto unsupported;
        VkPs4ImageView *view = fb->attachments[idx];
        if (!view || !view->image) goto unsupported;
        VkPs4Image *img = view->image;
        if (img->is_swapchain_image) {
            if (!img->swapchain_address || !img->swapchain_size || img->memory) goto unsupported;
        } else if (!img->memory || !img->memory->gnm_mem.mapped ||
                   img->memory_offset > img->memory->size) goto unsupported;
        if (!img->is_render_target ||
            (img->create_info.format != VK_FORMAT_R8G8B8A8_UNORM &&
             img->create_info.format != VK_FORMAT_R8G8B8A8_SRGB) ||
            img->create_info.imageType != VK_IMAGE_TYPE_2D ||
            img->create_info.samples != VK_SAMPLE_COUNT_1_BIT ||
            img->create_info.mipLevels != 1 || img->create_info.arrayLayers != 1) goto unsupported;
        VkRect2D area = cmd->current_render_pass.render_area;
        for (uint32_t r = 0; r < rectCount; ++r) {
            const VkClearRect *rect = &pRects[r];
            if (rect->baseArrayLayer || rect->layerCount != 1 ||
                rect->rect.offset.x < 0 || rect->rect.offset.y < 0 ||
                rect->rect.offset.x < area.offset.x || rect->rect.offset.y < area.offset.y ||
                (uint64_t)rect->rect.offset.x + rect->rect.extent.width > (uint64_t)area.offset.x + area.extent.width ||
                (uint64_t)rect->rect.offset.y + rect->rect.extent.height > (uint64_t)area.offset.y + area.extent.height ||
                (uint64_t)rect->rect.offset.x + rect->rect.extent.width > img->create_info.extent.width ||
                (uint64_t)rect->rect.offset.y + rect->rect.extent.height > img->create_info.extent.height)
                goto unsupported;
        }
        /* Full and partial color clears share the ordinary pipeline path. */
        vk_ps4_log("META CLEAR 1.24 rects=%u swapchain=%u",rectCount,img->is_swapchain_image);
        for (uint32_t r=0;r<rectCount;++r) {
            vk_ps4_meta_clear_rect(cmd,img,&att->clearValue.color,&pRects[r].rect);
            if(cmd->resource_error!=VK_SUCCESS) return;
        }
        vk_ps4_bind_subpass_targets(cmd);
    }
    return;
unsupported:
    cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT;
    return;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout,
                        VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size,
                        const void *pValues) {
    if (!commandBuffer || !pValues || size == 0) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    if ((offset & 3) || (size & 3) || offset>128 || size>128-offset) {
        cmd->resource_error=VK_ERROR_FEATURE_NOT_PRESENT; return;
    }
    for(unsigned stage=0;stage<2;++stage) {
        VkShaderStageFlags bit=stage ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        if(stageFlags & bit) for(unsigned n=0;n<size/4;++n) {
            unsigned word=offset/4+n;
            memcpy(&cmd->graphics_push_values[stage][word],(const char *)pValues+n*4,4);
            cmd->graphics_push_valid[stage] |= 1u<<word;
        }
    }
    VkPs4Pipeline *pipe = cmd->current_pipeline;
    if (!pipe) return;

    /* Push constants are emitted via SET_SH_REG PM4 packets to write
     * raw uint32_t values to the shader's user-data registers.
     * The pipeline's push_const_slots table (populated from
     * IMM_ALUFLOATCONST input usage slots) maps each push constant
     * dword index to a user-data register.
     * Optimization: consecutive user-data registers are batched into
     * a single SET_SH_REG packet to reduce PM4 overhead. */
    const uint32_t *values = (const uint32_t *)pValues;
    uint32_t start_dword = offset / 4;
    uint32_t end_dword = (offset + size + 3) / 4;

    /* Helper to emit a batched SET_SH_REG packet for consecutive regs.
     * Collects values into a small stack buffer, then emits one packet. */
    #define EMIT_PUSH_CONST_BATCH(gnm_cmd, reg_base, is_cs, slots, nslots) do { \
        uint32_t _buf[VK_PS4_MAX_PUSH_CONST_DWORDS]; \
        uint32_t _n = 0; \
        uint32_t _first_reg = 0; \
        for (uint32_t _i = 0; _i < (nslots); _i++) { \
            uint32_t _dw = (slots)[_i].dword_index; \
            if (_dw < start_dword || _dw >= end_dword) { \
                if (_n > 0) { \
                    uint32_t _need = 2 + _n; \
                    if ((uint32_t)((gnm_cmd)->endptr - (gnm_cmd)->cmdptr) < _need) { \
                        if ((gnm_cmd)->callback.func) \
                            (gnm_cmd)->callback.func(gnm_cmd, _need, (gnm_cmd)->callback.userdata); \
                    } \
                    if ((uint32_t)((gnm_cmd)->endptr - (gnm_cmd)->cmdptr) >= _need) { \
                        (gnm_cmd)->cmdptr[0] = PKT3(PKT3_SET_SH_REG, _n, 0) | \
                            ((is_cs) ? PKT3_SHADER_TYPE_S(1) : 0); \
                        (gnm_cmd)->cmdptr[1] = ((reg_base) + _first_reg * 4 - SI_SH_REG_OFFSET) >> 2; \
                        for (uint32_t _j = 0; _j < _n; _j++) \
                            (gnm_cmd)->cmdptr[2 + _j] = _buf[_j]; \
                        (gnm_cmd)->cmdptr += 2 + _n; \
                    } \
                    _n = 0; \
                } \
                continue; \
            } \
            uint32_t _reg = (slots)[_i].user_data_reg; \
            uint32_t _val = values[_dw - start_dword]; \
            if (_n == 0) { \
                _first_reg = _reg; \
                _buf[_n++] = _val; \
            } else if (_reg == _first_reg + _n) { \
                _buf[_n++] = _val; \
            } else { \
                uint32_t _need = 2 + _n; \
                if ((uint32_t)((gnm_cmd)->endptr - (gnm_cmd)->cmdptr) < _need) { \
                    if ((gnm_cmd)->callback.func) \
                        (gnm_cmd)->callback.func(gnm_cmd, _need, (gnm_cmd)->callback.userdata); \
                } \
                if ((uint32_t)((gnm_cmd)->endptr - (gnm_cmd)->cmdptr) >= _need) { \
                    (gnm_cmd)->cmdptr[0] = PKT3(PKT3_SET_SH_REG, _n, 0) | \
                        ((is_cs) ? PKT3_SHADER_TYPE_S(1) : 0); \
                    (gnm_cmd)->cmdptr[1] = ((reg_base) + _first_reg * 4 - SI_SH_REG_OFFSET) >> 2; \
                    for (uint32_t _j = 0; _j < _n; _j++) \
                        (gnm_cmd)->cmdptr[2 + _j] = _buf[_j]; \
                    (gnm_cmd)->cmdptr += 2 + _n; \
                } \
                _first_reg = _reg; \
                _n = 0; \
                _buf[_n++] = _val; \
            } \
        } \
        if (_n > 0) { \
            uint32_t _need = 2 + _n; \
            if ((uint32_t)((gnm_cmd)->endptr - (gnm_cmd)->cmdptr) < _need) { \
                if ((gnm_cmd)->callback.func) \
                    (gnm_cmd)->callback.func(gnm_cmd, _need, (gnm_cmd)->callback.userdata); \
            } \
            if ((uint32_t)((gnm_cmd)->endptr - (gnm_cmd)->cmdptr) >= _need) { \
                (gnm_cmd)->cmdptr[0] = PKT3(PKT3_SET_SH_REG, _n, 0) | \
                    ((is_cs) ? PKT3_SHADER_TYPE_S(1) : 0); \
                (gnm_cmd)->cmdptr[1] = ((reg_base) + _first_reg * 4 - SI_SH_REG_OFFSET) >> 2; \
                for (uint32_t _j = 0; _j < _n; _j++) \
                    (gnm_cmd)->cmdptr[2 + _j] = _buf[_j]; \
                (gnm_cmd)->cmdptr += 2 + _n; \
            } \
        } \
    } while (0)

    /* VS push constants */
    if ((stageFlags & VK_SHADER_STAGE_VERTEX_BIT) && pipe->vs_push_const_slot_count > 0) {
        EMIT_PUSH_CONST_BATCH(&cmd->gnm_cmd,
            R_00B130_SPI_SHADER_USER_DATA_VS_0, false,
            pipe->vs_push_const_slots, pipe->vs_push_const_slot_count);
    }

    /* PS push constants */
    if ((stageFlags & VK_SHADER_STAGE_FRAGMENT_BIT) && pipe->ps_push_const_slot_count > 0) {
        EMIT_PUSH_CONST_BATCH(&cmd->gnm_cmd,
            R_00B030_SPI_SHADER_USER_DATA_PS_0, false,
            pipe->ps_push_const_slots, pipe->ps_push_const_slot_count);
    }

    /* CS push constants */
    if ((stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) && pipe->cs_push_const_slot_count > 0) {
        EMIT_PUSH_CONST_BATCH(&cmd->gnm_cmd,
            R_00B900_COMPUTE_USER_DATA_0, true,
            pipe->cs_push_const_slots, pipe->cs_push_const_slot_count);
    }

    #undef EMIT_PUSH_CONST_BATCH
    (void)layout;
}

/* Query commands moved to vk_ps4_query.c */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdExecuteCommands(VkCommandBuffer commandBuffer,
                           uint32_t commandBufferCount,
                           const VkCommandBuffer *pCommandBuffers) {
    if (!commandBuffer || !pCommandBuffers || commandBufferCount == 0) return;
    VkPs4CommandBuffer *primary = (VkPs4CommandBuffer *)commandBuffer;

    /* CmdExecuteCommands copies PM4 data from each secondary command buffer
     * into the primary's PM4 stream. This is the simplest correct approach —
     * the GPU sees a single contiguous PM4 buffer at submit time.
     *
     * KNOWN LIMITATION: We don't handle VkCommandBufferInheritanceInfo yet.
     * Secondary buffers recorded with VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT
     * should inherit the primary's render pass / framebuffer, but we currently
     * ignore inheritance info and just copy the raw PM4. */
    for (uint32_t i = 0; i < commandBufferCount; i++) {
        VkPs4CommandBuffer *secondary = (VkPs4CommandBuffer *)pCommandBuffers[i];
        if (!secondary) continue;
        /* Skip secondary buffers that are still recording (not yet ended).
         * EndCommandBuffer sets is_recording=false, so a valid secondary
         * buffer that's ready to execute will have is_recording==false. */
        if (secondary->is_recording) continue;
        if (secondary->level != VK_COMMAND_BUFFER_LEVEL_SECONDARY) continue;

        /* Calculate the size of PM4 data in the secondary buffer */
        uint32_t *src_begin = secondary->gnm_cmd.beginptr;
        uint32_t *src_end = secondary->gnm_cmd.cmdptr;
        uint32_t src_dwords = (uint32_t)(src_end - src_begin);
        if (src_dwords == 0) continue;

        /* Check that the primary has enough space */
        uint32_t *dst_begin = primary->gnm_cmd.cmdptr;
        uint32_t *dst_end = primary->gnm_cmd.endptr;
        uint32_t dst_avail = (uint32_t)(dst_end - dst_begin);
        if (dst_avail < src_dwords) {
            /* Not enough space — skip this secondary buffer.
             * In a production driver we'd grow the buffer or return an error. */
            continue;
        }

        /* Copy the PM4 data from secondary to primary */
        memcpy(dst_begin, src_begin, src_dwords * sizeof(uint32_t));
        primary->gnm_cmd.cmdptr += src_dwords;
        primary->pm4_used += src_dwords;
    }
}
