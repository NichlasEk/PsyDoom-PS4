/*
 * vk_ps4_descriptor.c — Vulkan descriptor sets over GNM user-data registers.
 *
 * Vulkan descriptor sets map to GNM user-data register slots:
 *   VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER   → GnmBuffer (Vsharp, 4 regs)
 *   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER   → GnmBuffer (Vsharp, 4 regs)
 *   VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE    → GnmTexture (Tsharp, 8 regs)
 *   VK_DESCRIPTOR_TYPE_STORAGE_IMAGE    → GnmTexture (Tsharp, 8 regs)
 *   VK_DESCRIPTOR_TYPE_SAMPLER          → GnmSampler (Ssharp, 4 regs)
 *   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER → Tsharp + Ssharp
 *   VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER   → GnmBuffer (Vsharp)
 *   VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER   → GnmBuffer (Vsharp)
 *
 * The shader binary's GnmInputUsageSlot table (embedded by psbc) maps
 * Vulkan binding numbers (apislot) to GNM user-data registers
 * (startregister). At CmdBindDescriptorSets, we iterate the pipeline's
 * slot tables and emit Set*UserData PM4 commands.
 */

#include "vk_ps4_internal.h"
#include <string.h>
#include <stdio.h>
#include "psbc_compile.h"
static void publish_table(VkPs4DescriptorSet *set);

/* === Descriptor set layout === */
/* (Already implemented in vk_ps4_pipeline.c) */

/* === Descriptor pool === */
/* (Already implemented in vk_ps4_pipeline.c) */

/* === Descriptor set allocation === */
/* (Already implemented in vk_ps4_pipeline.c — but we need to initialize
 * the binding storage here. See vk_ps4_AllocateDescriptorSets below. */

/* === Helper: get GnmShaderStage from Vulkan bind point === */
static GnmShaderStage vk_bind_point_to_gnm_stage(VkPipelineBindPoint bp) {
    switch (bp) {
    case VK_PIPELINE_BIND_POINT_GRAPHICS: return GNM_STAGE_VS;
    case VK_PIPELINE_BIND_POINT_COMPUTE:  return GNM_STAGE_CS;
    default: return GNM_STAGE_VS;
    }
}

/* === Helper: find a binding in a descriptor set by Vulkan binding number === */
static VkPs4DescriptorBinding *find_binding(VkPs4DescriptorSet *set, uint32_t binding) {
    if (!set) return NULL;
    for (uint32_t i = 0; i < set->binding_count; i++) {
        if (set->bindings[i].binding_number == binding)
            return &set->bindings[i];
    }
    return NULL;
}

/* === Helper: allocate resource arrays for a binding === */
static VkResult alloc_binding_resources(VkPs4DescriptorBinding *b,
                                        const VkAllocationCallbacks *alloc) {
    if (b->count == 0) return VK_SUCCESS;
    switch (b->type) {
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        b->buffers = vk_ps4_alloc_zero(alloc, b->count * sizeof(GnmBuffer), 16);
        b->textures = NULL;
        b->samplers = NULL;
        if (!b->buffers) return VK_ERROR_OUT_OF_HOST_MEMORY;
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        b->samplers = vk_ps4_alloc_zero(alloc, b->count * sizeof(GnmSampler), 16);
        b->buffers = NULL;
        b->textures = NULL;
        if (!b->samplers) return VK_ERROR_OUT_OF_HOST_MEMORY;
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        b->textures = vk_ps4_alloc_zero(alloc, b->count * sizeof(GnmTexture), 16);
        b->buffers = NULL;
        b->samplers = NULL;
        if (!b->textures) return VK_ERROR_OUT_OF_HOST_MEMORY;
        break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        b->textures = vk_ps4_alloc_zero(alloc, b->count * sizeof(GnmTexture), 16);
        if (!b->textures) return VK_ERROR_OUT_OF_HOST_MEMORY;
        b->samplers = vk_ps4_alloc_zero(alloc, b->count * sizeof(GnmSampler), 16);
        if (!b->samplers) {
            vk_ps4_free(alloc, b->textures);
            b->textures = NULL;
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        b->buffers = NULL;
        break;
    default:
        /* Unsupported descriptor type */
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    return VK_SUCCESS;
}

/* === Helper: free resource arrays for a binding === */
static void free_binding_resources(VkPs4DescriptorBinding *b,
                                   const VkAllocationCallbacks *alloc) {
    if (b->buffers) { vk_ps4_free(alloc, b->buffers); b->buffers = NULL; }
    if (b->textures) { vk_ps4_free(alloc, b->textures); b->textures = NULL; }
    if (b->samplers) { vk_ps4_free(alloc, b->samplers); b->samplers = NULL; }
}

/* === Helper: build a GnmBuffer from a VkDescriptorBufferInfo === */
static void build_buffer_descriptor(GnmBuffer *gnm_buf, VkPs4Buffer *vk_buf,
                                     VkDeviceSize offset, VkDeviceSize range,
                                     VkDescriptorType desc_type) {
    if (!vk_buf || !vk_buf->memory || !vk_buf->memory->gnm_mem.mapped) {
        memset(gnm_buf, 0, sizeof(*gnm_buf));
        return;
    }

    void *gpu_addr = (char *)vk_buf->memory->gnm_mem.mapped +
                     vk_buf->memory_offset + offset;

    /* Use sceGnmCreateConstBuffer as a base for UBOs, or create a raw
     * buffer for SSBOs. For MVP, both use the same const buffer path. */
    uint32_t size;
    if (range == VK_WHOLE_SIZE) {
        if (offset > vk_buf->create_info.size) {
            memset(gnm_buf, 0, sizeof(*gnm_buf));
            return;
        }
        size = (uint32_t)(vk_buf->create_info.size - offset);
    } else {
        size = (uint32_t)range;
    }

    *gnm_buf = sceGnmCreateConstBuffer(gpu_addr, size);

    /* For storage buffers, set stride to 0 (raw buffer) */
    if (desc_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
        desc_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
        gnm_buf->stride = 0;
    }
}

/* === Helper: build a GnmSampler from VkSamplerCreateInfo fields === */
static void build_sampler_descriptor(GnmSampler *s, const VkSamplerCreateInfo *ci) {
    memset(s, 0, sizeof(*s));

    /* Clamp modes — GnmTexClamp enum values are non-sequential. */
    static const GnmTexClamp vk_to_gnm_clamp[] = {
        [VK_SAMPLER_ADDRESS_MODE_REPEAT] = GNM_TEX_CLAMP_WRAP,
        [VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT] = GNM_TEX_CLAMP_MIRROR,
        [VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE] = GNM_TEX_CLAMP_CLAMP_LAST_TEXEL,
        [VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER] = GNM_TEX_CLAMP_CLAMP_BORDER,
        [VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE] = GNM_TEX_CLAMP_MIRROR_ONCE_LAST_TEXEL,
    };
    if (ci->addressModeU < sizeof(vk_to_gnm_clamp)/sizeof(vk_to_gnm_clamp[0]))
        s->clampx = vk_to_gnm_clamp[ci->addressModeU];
    if (ci->addressModeV < sizeof(vk_to_gnm_clamp)/sizeof(vk_to_gnm_clamp[0]))
        s->clampy = vk_to_gnm_clamp[ci->addressModeV];
    if (ci->addressModeW < sizeof(vk_to_gnm_clamp)/sizeof(vk_to_gnm_clamp[0]))
        s->clampz = vk_to_gnm_clamp[ci->addressModeW];

    /* Filter modes — GnmFilter: POINT=0, BILINEAR=1, ANISO_POINT=2, ANISO_BILINEAR=3.
     * When anisotropy is enabled, use ANISO_* variants. */
    s->filtermode = 0; /* 0 = sync, 1 = async */
    if (ci->anisotropyEnable) {
        s->xymagfilter = (ci->magFilter == VK_FILTER_LINEAR)
            ? GNM_FILTER_ANISO_BILINEAR : GNM_FILTER_ANISO_POINT;
        s->xyminfilter = (ci->minFilter == VK_FILTER_LINEAR)
            ? GNM_FILTER_ANISO_BILINEAR : GNM_FILTER_ANISO_POINT;
    } else {
        s->xymagfilter = (ci->magFilter == VK_FILTER_LINEAR)
            ? GNM_FILTER_BILINEAR : GNM_FILTER_POINT;
        s->xyminfilter = (ci->minFilter == VK_FILTER_LINEAR)
            ? GNM_FILTER_BILINEAR : GNM_FILTER_POINT;
    }
    /* GnmMipFilter: NONE=0, POINT=1, LINEAR=2 */
    if (ci->mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR) {
        s->mipfilter = GNM_MIPFILTER_LINEAR;
    } else {
        s->mipfilter = GNM_MIPFILTER_POINT;
    }

    /* LOD — clamp to non-negative for min/max LOD. lodbias is s4.10 fixed-point. */
    float min_lod = ci->minLod > 0.0f ? ci->minLod : 0.0f;
    float max_lod = ci->maxLod > 0.0f ? ci->maxLod : 0.0f;
    s->minlod = (uint32_t)(min_lod * 256.0f) & 0xFFF;
    s->maxlod = (uint32_t)(max_lod * 256.0f) & 0xFFF;
    /* lodbias is a signed 14-bit s4.10 fixed-point (10 fractional bits → scale 1024). */
    int32_t bias = (int32_t)(ci->mipLodBias * 1024.0f);
    s->lodbias = (uint32_t)(bias & 0x3FFF);

    /* Anisotropic filtering — GNM maxanisoratio is a 3-bit index:
     * 0=1x, 1=2x, 2=4x, 3=8x, 4=16x. */
    if (ci->anisotropyEnable) {
        float a = ci->maxAnisotropy;
        if (a >= 16.0f) s->maxanisoratio = 4;
        else if (a >= 8.0f) s->maxanisoratio = 3;
        else if (a >= 4.0f) s->maxanisoratio = 2;
        else if (a >= 2.0f) s->maxanisoratio = 1;
        else s->maxanisoratio = 0;
    }

    /* Compare */
    if (ci->compareEnable) {
        /* Map VkCompareOp to GNM depth compare function.
         * GNM uses the same encoding as VkCompareOp (0=NEVER, 1=LESS, etc.) */
        static const uint32_t vk_to_gnm_compare[] = {
            [VK_COMPARE_OP_NEVER] = 0,
            [VK_COMPARE_OP_LESS] = 1,
            [VK_COMPARE_OP_EQUAL] = 2,
            [VK_COMPARE_OP_LESS_OR_EQUAL] = 3,
            [VK_COMPARE_OP_GREATER] = 4,
            [VK_COMPARE_OP_NOT_EQUAL] = 5,
            [VK_COMPARE_OP_GREATER_OR_EQUAL] = 6,
            [VK_COMPARE_OP_ALWAYS] = 7,
        };
        if (ci->compareOp < sizeof(vk_to_gnm_compare)/sizeof(vk_to_gnm_compare[0]))
            s->depthcomparefunc = vk_to_gnm_compare[ci->compareOp];
    } else {
        s->depthcomparefunc = 7; /* ALWAYS — no comparison */
    }

    /* Border color — GnmBorderColor: TRANS_BLACK=0, OPAQUE_BLACK=1, OPAQUE_WHITE=2. */
    if (ci->addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
        ci->addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
        ci->addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER) {
        switch (ci->borderColor) {
        case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
        case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
            s->bordercolortype = GNM_BORDER_COLOR_TRANS_BLACK;
            break;
        case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
        case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
            s->bordercolortype = GNM_BORDER_COLOR_OPAQUE_WHITE;
            break;
        case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
        case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
        default:
            s->bordercolortype = GNM_BORDER_COLOR_OPAQUE_BLACK;
            break;
        }
    }

    /* Force normalized coordinates (Vulkan always uses normalized) */
    s->forceunormalized = 0;
}

/* === vkUpdateDescriptorSets === */
VKAPI_ATTR void VKAPI_CALL
vk_ps4_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                            const VkWriteDescriptorSet *pDescriptorWrites,
                            uint32_t descriptorCopyCount,
                            const VkCopyDescriptorSet *pDescriptorCopies) {
    if (!device) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;

    /* Process writes */
    for (uint32_t i = 0; i < descriptorWriteCount; i++) {
        const VkWriteDescriptorSet *w = &pDescriptorWrites[i];
        VkPs4DescriptorSet *set = (VkPs4DescriptorSet *)w->dstSet;
        if (!set) continue;

        VkPs4DescriptorBinding *b = find_binding(set, w->dstBinding);
        if (!b || b->count == 0) continue;

        /* Ensure the binding has resource arrays allocated.
         * AllocateDescriptorSets sets type/count/binding_number but doesn't
         * allocate the resource arrays — that happens lazily here.
         * Never overwrite b->count, b->binding_number, or b->type from the
         * write — those come from the layout and are immutable. */
        if (!b->resources_allocated) {
            if (alloc_binding_resources(b, alloc) != VK_SUCCESS) {
                continue;
            }
            b->resources_allocated = true;
        }

        /* Clamp write range to binding array bounds (avoid underflow) */
        uint32_t dst_start = w->dstArrayElement;
        uint32_t count = w->descriptorCount;
        if (dst_start >= b->count) continue;
        if (dst_start + count > b->count) {
            count = b->count - dst_start;
        }

        /* Use b->type (layout-declared type) for the switch, not w->descriptorType.
         * This prevents NULL deref if a second write uses a different type. */
        switch (b->type) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
            for (uint32_t j = 0; j < count; j++) {
                const VkDescriptorBufferInfo *bi = &w->pBufferInfo[j];
                VkPs4Buffer *vk_buf = (VkPs4Buffer *)bi->buffer;
                if (!vk_buf) {
                    memset(&b->buffers[dst_start + j], 0, sizeof(GnmBuffer));
                    continue;
                }
                build_buffer_descriptor(&b->buffers[dst_start + j], vk_buf,
                                        bi->offset, bi->range, b->type);
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: {
            for (uint32_t j = 0; j < count; j++) {
                const VkDescriptorImageInfo *ii = &w->pImageInfo[j];
                VkPs4ImageView *view = (VkPs4ImageView *)ii->imageView;
                if (view && view->image) {
                    /* Use the view's GnmTexture descriptor for both regular
                     * textures and render targets.  RT-as-texture is handled
                     * in CreateImageView which builds a GnmTexture from the
                     * GnmRenderTarget. */
                    b->textures[dst_start + j] = view->gnm_view;
                } else {
                    memset(&b->textures[dst_start + j], 0, sizeof(GnmTexture));
                }
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_SAMPLER: {
            for (uint32_t j = 0; j < count; j++) {
                const GnmSampler *s = (const GnmSampler *)w->pImageInfo[j].sampler;
                if (s) {
                    b->samplers[dst_start + j] = *s;
                } else {
                    memset(&b->samplers[dst_start + j], 0, sizeof(GnmSampler));
                }
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
            for (uint32_t j = 0; j < count; j++) {
                const VkDescriptorImageInfo *ii = &w->pImageInfo[j];
                VkPs4ImageView *view = (VkPs4ImageView *)ii->imageView;
                const GnmSampler *s = (const GnmSampler *)ii->sampler;
                if (view && view->image) {
                    b->textures[dst_start + j] = view->gnm_view;
                } else {
                    memset(&b->textures[dst_start + j], 0, sizeof(GnmTexture));
                }
                if (s) {
                    b->samplers[dst_start + j] = *s;
                } else {
                    memset(&b->samplers[dst_start + j], 0, sizeof(GnmSampler));
                }
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
            /* Texel buffer views: copy the GnmBuffer (V#) descriptor from
             * each VkBufferView into the binding's buffer array.  The V#
             * was built in CreateBufferView with the correct base address,
             * format, stride, and num records. */
            for (uint32_t j = 0; j < count; j++) {
                VkPs4BufferView *bv = (VkPs4BufferView *)w->pTexelBufferView[j];
                if (bv) {
                    b->buffers[dst_start + j] = bv->gnm_buffer;
                } else {
                    memset(&b->buffers[dst_start + j], 0, sizeof(GnmBuffer));
                }
            }
            break;
        }
        default:
            break;
        }
    }

    /* Process copies */
    for (uint32_t i = 0; i < descriptorCopyCount; i++) {
        const VkCopyDescriptorSet *c = &pDescriptorCopies[i];
        VkPs4DescriptorSet *dst = (VkPs4DescriptorSet *)c->dstSet;
        VkPs4DescriptorSet *src = (VkPs4DescriptorSet *)c->srcSet;
        if (!dst || !src) continue;

        VkPs4DescriptorBinding *db = find_binding(dst, c->dstBinding);
        VkPs4DescriptorBinding *sb = find_binding(src, c->srcBinding);
        if (!db || !sb || db->type != sb->type) continue;

        /* Ensure dst resources are allocated */
        if (!db->resources_allocated) {
            if (alloc_binding_resources(db, alloc) != VK_SUCCESS)
                continue;
            db->resources_allocated = true;
        }

        /* Clamp copy count (avoid underflow) */
        uint32_t count = c->descriptorCount;
        if (c->dstArrayElement >= db->count || c->srcArrayElement >= sb->count)
            continue;
        if (c->dstArrayElement + count > db->count)
            count = db->count - c->dstArrayElement;
        if (c->srcArrayElement + count > sb->count)
            count = sb->count - c->srcArrayElement;

        if (db->buffers && sb->buffers) {
            memcpy(&db->buffers[c->dstArrayElement],
                   &sb->buffers[c->srcArrayElement],
                   count * sizeof(GnmBuffer));
        }
        if (db->textures && sb->textures) {
            memcpy(&db->textures[c->dstArrayElement],
                   &sb->textures[c->srcArrayElement],
                   count * sizeof(GnmTexture));
        }
        if (db->samplers && sb->samplers) {
            memcpy(&db->samplers[c->dstArrayElement],
                   &sb->samplers[c->srcArrayElement],
                   count * sizeof(GnmSampler));
        }
    }
    for (uint32_t i = 0; i < descriptorWriteCount; ++i)
        publish_table((VkPs4DescriptorSet *)pDescriptorWrites[i].dstSet);
    for (uint32_t i = 0; i < descriptorCopyCount; ++i)
        publish_table((VkPs4DescriptorSet *)pDescriptorCopies[i].dstSet);

}

/* Set-0 ABI is validated against the pipeline layout by psbc. The table
 * belongs to the descriptor set and lives until free/reset/pool destruction.
 * Vulkan requires clients to synchronize updates/free with pending GPU use. */
static void publish_table(VkPs4DescriptorSet *set) {
    if (!set) return;
    uint8_t *table = set->resource_table.mapped;
    for (uint32_t i = 0; i < set->binding_count; ++i) {
        VkPs4DescriptorBinding *b = &set->bindings[i];
        if (b->buffers) memcpy(table + b->binding_number * 16, b->buffers, sizeof(GnmBuffer));
        if (b->textures) memcpy(table + PSBC_TEXTURE_TABLE_OFFSET + b->binding_number * PSBC_TEXTURE_BINDING_STRIDE,
                               b->textures, sizeof(GnmTexture));
        if (b->samplers) memcpy(table + PSBC_TEXTURE_TABLE_OFFSET + b->binding_number * PSBC_TEXTURE_BINDING_STRIDE + PSBC_COMBINED_SAMPLER_OFFSET,
                               b->samplers, sizeof(GnmSampler));
    }
}
VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint point,
    VkPipelineLayout layout, uint32_t firstSet, uint32_t count, const VkDescriptorSet *sets,
    uint32_t dynamicCount, const uint32_t *offsets) {
    (void)layout; (void)offsets;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    if (point != VK_PIPELINE_BIND_POINT_GRAPHICS || firstSet || count > 1 || dynamicCount) {
        cmd->resource_error = VK_ERROR_FEATURE_NOT_PRESENT; return;
    }
    if (count) cmd->graphics_set0 = (VkPs4DescriptorSet *)sets[0];
}
bool vk_ps4_emit_resource_table(VkPs4CommandBuffer *cmd) {
    if (cmd->resource_error != VK_SUCCESS) return false;
    VkPs4Pipeline *p = cmd->current_pipeline;
    if (!p) return false;
    for (unsigned stage = 0; stage < 2; ++stage) {
        const GnmInputUsageSlot *slots = stage ? p->ps_input_usage_slots : p->vs_input_usage_slots;
        uint32_t count = stage ? p->ps_input_usage_slot_count : p->vs_input_usage_slot_count;
        for (uint32_t i = 0; i < count; ++i) {
            if (slots[i].usagetype != GNM_SHINPUTUSAGE_PTR_INDIRECTRESOURCETABLE) continue;
            if (!cmd->graphics_set0) { cmd->resource_error = VK_ERROR_INITIALIZATION_FAILED; return false; }
            sceGnmDrawCmdSetPointerUserData(&cmd->gnm_cmd, stage ? GNM_STAGE_PS : GNM_STAGE_VS,
                slots[i].startregister, cmd->graphics_set0->resource_table.mapped);
        }
    }
    return true;
}

/* === Sampler === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator, VkSampler *pSampler) {
    if (!device || !pCreateInfo || !pSampler) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    GnmSampler *sampler = vk_ps4_alloc_zero(alloc, sizeof(*sampler), 16);
    if (!sampler) return VK_ERROR_OUT_OF_HOST_MEMORY;

    build_sampler_descriptor(sampler, pCreateInfo);

    *pSampler = (VkSampler)sampler;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks *pAllocator) {
    if (!device || !sampler) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, (void *)sampler);
}
