#ifndef RADV_PIPELINE_H
#define RADV_PIPELINE_H

#include "nir/radv_nir.h"

enum radv_pipeline_type {
   RADV_PIPELINE_GRAPHICS = 0,
   RADV_PIPELINE_GRAPHICS_LIB,
   RADV_PIPELINE_COMPUTE,
   RADV_PIPELINE_RAY_TRACING,
};

struct radv_pipeline {
   enum radv_pipeline_type type;
};

bool radv_shader_should_clear_lds(const struct radv_compiler_info *compiler_info,
                                  const nir_shader *shader);

void radv_postprocess_nir(const struct radv_compiler_info *compiler_info,
                          const struct radv_graphics_state_key *gfx_state,
                          struct radv_shader_stage *stage);

#endif
