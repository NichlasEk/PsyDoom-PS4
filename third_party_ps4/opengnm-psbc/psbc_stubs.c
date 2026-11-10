/*
 * Stub implementations for functions from excluded Mesa/RADV files.
 * These are needed because radv_shader.c and ac_cmdbuf.c reference
 * functions from video, ray tracing, and Vulkan runtime files that
 * are too complex to build standalone.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "nir.h"
#include "util/macros.h"

/* === From ac_gpu_info.c (excluded — needs Linux DRM/addrlib) === */
#include "ac_gpu_info.h"
void ac_get_harvested_configs(const struct radeon_info *info, unsigned raster_config,
                              unsigned *raster_config_1, unsigned *raster_config_se)
{
   if (raster_config_1)
      *raster_config_1 = 0;
   if (raster_config_se)
      memset(raster_config_se, 0, sizeof(unsigned) * 4);
}

uint32_t ac_gfx103_get_cu_mask_ps(const struct radeon_info *info)
{
   return ~0u;
}

/* === From ac_vcn_dec.c (excluded — video decode) === */
#include "ac_vcn_dec.h"
struct ac_video_dec *ac_vcn_create_jpeg_decoder(const struct radeon_info *info,
                                                 struct ac_video_dec_session_param *param)
{
   return NULL;
}

struct ac_video_dec *ac_vcn_create_video_decoder(const struct radeon_info *info,
                                                  struct ac_video_dec_session_param *param)
{
   return NULL;
}

uint32_t ac_vcn_dec_dpb_alignment(const struct radeon_info *info,
                                   struct ac_video_dec_session_param *param)
{
   return 0;
}

uint32_t ac_vcn_dec_dpb_size(const struct radeon_info *info,
                              struct ac_video_dec_session_param *param)
{
   return 0;
}

void ac_vcn_dec_init_regs(struct ac_vcn_dec_reg *reg, enum vcn_version version)
{
   memset(reg, 0, sizeof(*reg));
}

/* === From vk_shader.c / vk_nir.c (excluded — needs full Vulkan runtime) === */
#include "vk_util.h"
struct nir_spirv_specialization *vk_spec_info_to_nir_spirv(const VkSpecializationInfo *vk_spec_info)
{
   return NULL;
}

#include "vk_nir.h"
uint32_t vk_spirv_version(const uint32_t *spirv_data, size_t spirv_size_B)
{
   if (spirv_size_B < 4)
      return 0;
   return spirv_data[1];
}

bool nir_vk_is_not_xfb_output(nir_variable *var, void *data)
{
   return true;
}

/* === From vk_debug_report.c (excluded) === */
#include "vk_debug_report.h"
void vk_debug_report(struct vk_debug_report *debug_report,
                     VkDebugReportFlagsEXT flags,
                     const struct vk_object_base *object,
                     size_t location,
                     int32_t messageCode,
                     const char *pLayerPrefix,
                     const char *pMessage)
{
   /* no-op */
}

/* === From radv_nir_rt_stage_functions.c (excluded — ray tracing) === */
nir_function_impl *radv_get_rt_shader_entrypoint(nir_shader *shader)
{
   return nir_shader_get_entrypoint(shader);
}

/* === From radv_nir_lower_ray_queries.c (excluded — ray tracing) === */
#include "radv_nir.h"
bool radv_nir_lower_ray_queries(nir_shader *shader, const struct radv_compiler_info *compiler_info)
{
   return false;
}

/* === From tools/radv_debug_nir.c (excluded — needs full driver) === */
#include "tools/radv_debug_nir.h"
void radv_build_printf_args(struct radv_debug_nir *debug_nir, nir_builder *b,
                            const char *format, uint32_t argc,
                            nir_def **args)
{
   /* no-op */
}

/* === From radv_sampler.c (excluded — needs full driver) === */
#include "radv_sampler.h"
void radv_make_sampler_descriptor(const struct radv_compiler_info *compiler_info,
                                  const struct vk_sampler_state *sampler_state,
                                  uint32_t *desc)
{
   memset(desc, 0, 16);
}

void vk_sampler_state_init(struct vk_sampler_state *state,
                           const VkSamplerCreateInfo *pCreateInfo)
{
   memset(state, 0, sizeof(*state));
}

/* === From vk_format.c (excluded — compilation errors) === */
#include "vk_format.h"
enum pipe_format vk_format_to_pipe_format(VkFormat vkformat)
{
   return PIPE_FORMAT_NONE;
}

const struct vk_format_ycbcr_info *vk_format_get_ycbcr_info(VkFormat format)
{
   return NULL;
}

/* === radv_parse_binary_debug_info — disabled by #if 0 in radv_shader.c === */
#include "radv_shader.h"
VkResult radv_parse_binary_debug_info(const struct radv_compiler_info *compiler_info,
                                      const struct radv_shader_binary *binary,
                                      struct radv_shader_debug_info *dbg)
{
   return VK_SUCCESS;
}
