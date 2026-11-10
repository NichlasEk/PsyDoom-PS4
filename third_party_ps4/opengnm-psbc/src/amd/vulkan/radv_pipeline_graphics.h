#ifndef RADV_PIPELINE_GRAPHICS_H
#define RADV_PIPELINE_GRAPHICS_H

#include "amdgfxregs.h"
#include "radv_shader.h"

static inline uint32_t
radv_conv_prim_to_gs_out(uint32_t topology, bool is_ngg)
{
   switch (topology) {
   case V_008958_DI_PT_POINTLIST:
   case V_008958_DI_PT_PATCH:
      return V_028A6C_POINTLIST;
   case V_008958_DI_PT_LINELIST:
   case V_008958_DI_PT_LINESTRIP:
   case V_008958_DI_PT_LINELIST_ADJ:
   case V_008958_DI_PT_LINESTRIP_ADJ:
      return V_028A6C_LINESTRIP;
   case V_008958_DI_PT_TRILIST:
   case V_008958_DI_PT_TRISTRIP:
   case V_008958_DI_PT_TRIFAN:
   case V_008958_DI_PT_TRILIST_ADJ:
   case V_008958_DI_PT_TRISTRIP_ADJ:
      return V_028A6C_TRISTRIP;
   case V_008958_DI_PT_RECTLIST:
      return is_ngg ? V_028A6C_RECTLIST : V_028A6C_TRISTRIP;
   default:
      assert(0);
      return 0;
   }
}

static inline unsigned
radv_get_num_vertices_per_prim(const struct radv_graphics_state_key *gfx_state)
{
   if (gfx_state->ia.topology == V_008958_DI_PT_NONE) {
      return 3;
   } else {
      return radv_conv_prim_to_gs_out(gfx_state->ia.topology, false) + 1;
   }
}

#endif
