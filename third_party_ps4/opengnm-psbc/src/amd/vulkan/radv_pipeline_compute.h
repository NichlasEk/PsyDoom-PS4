#ifndef RADV_PIPELINE_COMPUTE_H
#define RADV_PIPELINE_COMPUTE_H

#include "radv_physical_device.h"
#include "radv_shader_info.h"
#include "ac_shader_util.h"

static inline uint32_t
radv_get_compute_resource_limits(const struct radv_physical_device *pdev, const struct radv_shader_info *info)
{
   unsigned threads_per_threadgroup = info->cs.block_size[0] * info->cs.block_size[1] * info->cs.block_size[2];
   unsigned waves_per_threadgroup = DIV_ROUND_UP(threads_per_threadgroup, info->wave_size);
   unsigned threadgroups_per_cu = 1;

   if (pdev->info.gfx_level >= GFX10 && waves_per_threadgroup == 1)
      threadgroups_per_cu = 2;

   return ac_get_compute_resource_limits(&pdev->info, waves_per_threadgroup, 0, threadgroups_per_cu);
}

#endif
