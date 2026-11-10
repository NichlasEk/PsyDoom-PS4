/*
 * Extracted from radv_pipeline.c for standalone psbc compilation.
 * Contains only radv_postprocess_nir and its helpers.
 */

#include "radv_pipeline.h"
#include "nir/nir.h"
#include "nir/radv_nir.h"
#include "radv_shader.h"
#include "radv_shader_args.h"
#include "vk_util.h"

#include "ac_binary.h"
#include "ac_nir.h"
#include "ac_shader_util.h"
#include "aco_interface.h"

#include "sid.h"

static nir_component_mask_t
non_uniform_access_callback(const nir_src *src, void *_)
{
   if (src->ssa->num_components == 1)
      return 0x1;
   return nir_chase_binding(*src).success ? 0x2 : 0x3;
}

void
radv_postprocess_nir(const struct radv_compiler_info *compiler_info, const struct radv_graphics_state_key *gfx_state,
                     struct radv_shader_stage *stage)
{
   enum amd_gfx_level gfx_level = compiler_info->ac->gfx_level;
   const bool use_llvm = compiler_info->key.use_llvm;
   bool progress;

   /* Wave and workgroup size should already be filled. */
   assert(stage->info.wave_size && stage->info.workgroup_size);

   if (stage->stage == MESA_SHADER_FRAGMENT) {
      if (!stage->key.optimisations_disabled) {
         NIR_PASS(_, stage->nir, nir_opt_cse);
      }
      NIR_PASS(_, stage->nir, radv_nir_lower_fs_intrinsics, stage, gfx_state);
   }

   /* LLVM could support more of these in theory. */
   radv_nir_opt_tid_function_options tid_options = {
      .use_masked_swizzle_amd = true,
      .use_dpp16_shift_amd = !use_llvm && gfx_level >= GFX8,
      .use_clustered_rotate = !use_llvm,
      .hw_subgroup_size = stage->info.wave_size,
      .hw_ballot_bit_size = stage->info.wave_size,
      .hw_ballot_num_comp = 1,
   };
   NIR_PASS(_, stage->nir, radv_nir_opt_tid_function, &tid_options);

   NIR_PASS(_, stage->nir, ac_nir_flag_smem_for_loads, gfx_level, use_llvm);

   NIR_PASS(_, stage->nir, nir_lower_memory_model);

   nir_load_store_vectorize_options vectorize_opts = {
      .modes = nir_var_mem_ssbo | nir_var_mem_ubo | nir_var_mem_push_const | nir_var_mem_shared | nir_var_mem_global |
               nir_var_shader_temp,
      .callback = ac_nir_mem_vectorize_callback,
      .cb_data = &(struct ac_nir_config){gfx_level, !use_llvm},
      .robust_modes = 0,
      .bounds_checked_modes = nir_var_mem_ssbo | nir_var_mem_ubo | nir_var_mem_shared,
      /* Only vectorize shared2 during late optimizations. */
      .has_shared2_amd = false,
   };

   if (stage->key.uniform_robustness2)
      vectorize_opts.robust_modes |= nir_var_mem_ubo;

   if (stage->key.storage_robustness2)
      vectorize_opts.robust_modes |= nir_var_mem_ssbo;

   bool constant_fold_for_push_const = false;
   if (!stage->key.optimisations_disabled) {
      progress = false;
      NIR_PASS(progress, stage->nir, nir_opt_load_store_vectorize, &vectorize_opts);
      if (progress) {
         NIR_PASS(_, stage->nir, nir_opt_copy_prop);
         NIR_PASS(_, stage->nir, nir_opt_shrink_stores, !compiler_info->key.disable_shrink_image_store);

         constant_fold_for_push_const = true;
      }
   }

   enum nir_lower_non_uniform_access_type lower_non_uniform_access_types =
      nir_lower_non_uniform_ubo_access | nir_lower_non_uniform_ssbo_access | nir_lower_non_uniform_texture_access |
      nir_lower_non_uniform_image_access | nir_lower_non_uniform_texture_query | nir_lower_non_uniform_image_query;

   /* In practice, most shaders do not have non-uniform-qualified
    * accesses (see
    * https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/17558#note_1475069)
    * thus a cheaper and likely to fail check is run first.
    */
   if (nir_has_non_uniform_access(stage->nir, lower_non_uniform_access_types)) {
      if (!stage->key.optimisations_disabled) {
         NIR_PASS(_, stage->nir, nir_opt_non_uniform_access);
      }

      if (!use_llvm) {
         nir_lower_non_uniform_access_options options = {
            .types = lower_non_uniform_access_types,
            .callback = &non_uniform_access_callback,
            .callback_data = NULL,
         };
         NIR_PASS(_, stage->nir, nir_lower_non_uniform_access, &options);
      }
   }

   progress = false;
   NIR_PASS(progress, stage->nir, ac_nir_lower_mem_access_bit_sizes, gfx_level, use_llvm);
   if (progress)
      constant_fold_for_push_const = true;

   NIR_PASS(_, stage->nir, ac_nir_lower_tex_coords,
            &(ac_nir_lower_tex_coords_options){
               .gfx_level = gfx_level,
               .lower_array_layer_round_even =
                  !compiler_info->ac->conformant_trunc_coord && !compiler_info->key.disable_trunc_coord,
               .fix_derivs_in_divergent_cf = stage->stage == MESA_SHADER_FRAGMENT && !use_llvm,
               .max_wqm_vgprs = 64,
            });

   NIR_PASS(_, stage->nir, ac_nir_lower_image_tex,
            &(ac_nir_lower_image_tex_options){
               .gfx_level = gfx_level,
            });

   if (stage->nir->info.uses_resource_info_query)
      NIR_PASS(_, stage->nir, ac_nir_lower_resinfo, gfx_level);

   /* Ensure split load_push_constant still have constant offsets, for radv_nir_lower_descriptors. */
   if (constant_fold_for_push_const && stage->args.ac.inline_push_const_mask)
      NIR_PASS(_, stage->nir, nir_opt_constant_folding);

   /* Optimize NIR before NGG culling */
   bool is_last_vgt_stage = radv_is_last_vgt_stage(stage);
   bool lowered_ngg = stage->info.is_ngg && is_last_vgt_stage;
   if (lowered_ngg && stage->nir->info.stage != MESA_SHADER_GEOMETRY && stage->info.has_ngg_culling)
      radv_optimize_nir_algebraic_early(stage->nir);

   /* This has to be done after nir_opt_algebraic for best descriptor vectorization, but also before
    * NGG culling.
    */
   NIR_PASS(_, stage->nir, radv_nir_lower_descriptors, compiler_info, stage);

   NIR_PASS(_, stage->nir, nir_lower_alu_width, ac_nir_opt_vectorize_cb, &gfx_level);

   nir_move_options sink_opts = nir_move_const_undef | nir_move_copies | nir_dont_move_byte_word_vecs;

   if (!stage->key.optimisations_disabled) {
      NIR_PASS(_, stage->nir, nir_opt_licm, NULL);

      if (stage->stage == MESA_SHADER_VERTEX) {
         NIR_PASS(_, stage->nir, nir_opt_move_to_top, nir_move_to_top_input_loads_simple);
         NIR_PASS(_, stage->nir, nir_opt_sink, sink_opts);
         NIR_PASS(_, stage->nir, nir_opt_move, sink_opts);
      } else {
         if (stage->stage != MESA_SHADER_FRAGMENT || !compiler_info->key.disable_sinking_load_input_fs)
            sink_opts |= nir_move_load_input | nir_move_load_frag_coord;

         NIR_PASS(_, stage->nir, nir_opt_sink, sink_opts);
         NIR_PASS(_, stage->nir, nir_opt_move, sink_opts | nir_move_load_input | nir_move_load_frag_coord);
      }
   }

   /* Lower VS inputs. We need to do this after nir_opt_sink, because
    * load_input can be reordered, but buffer loads can't.
    */
   if (stage->stage == MESA_SHADER_VERTEX) {
      NIR_PASS(_, stage->nir, radv_nir_lower_vs_inputs, compiler_info, stage, gfx_state);
   }

   /* Lower I/O intrinsics to memory instructions. */
   bool io_to_mem = radv_nir_lower_io_to_mem(compiler_info, stage);
   if (lowered_ngg) {
      radv_lower_ngg(compiler_info, stage, gfx_state);
   } else if (is_last_vgt_stage) {
      if (stage->stage != MESA_SHADER_GEOMETRY) {
         NIR_PASS(_, stage->nir, ac_nir_lower_legacy_vs, gfx_level,
                  stage->info.outinfo.clip_dist_mask | stage->info.outinfo.cull_dist_mask, false,
                  stage->info.outinfo.vs_output_param_offset, stage->info.outinfo.param_exports,
                  stage->info.outinfo.export_prim_id, false, stage->info.force_vrs_per_vertex);

      } else {
         ac_nir_lower_legacy_gs_options options = {
            .has_gen_prim_query = false,
            .has_pipeline_stats_query = false,
            .gfx_level = gfx_level,
            .export_clipdist_mask = stage->info.outinfo.clip_dist_mask | stage->info.outinfo.cull_dist_mask,
            .param_offsets = stage->info.outinfo.vs_output_param_offset,
            .has_param_exports = stage->info.outinfo.param_exports,
            .force_vrs = stage->info.force_vrs_per_vertex,
         };
         ac_nir_legacy_gs_info info = {0};

         NIR_PASS(_, stage->nir, ac_nir_lower_legacy_gs, &options, &stage->gs_copy_shader, &info);

         for (unsigned i = 0; i < 4; i++)
            stage->info.gs.num_components_per_stream[i] = info.num_components_per_stream[i];
      }
   } else if (stage->stage == MESA_SHADER_FRAGMENT) {
      ac_nir_lower_ps_late_options late_options = {
         .gfx_level = gfx_level,
         .use_aco = !use_llvm,
         .bc_optimize_for_persp = G_0286CC_PERSP_CENTER_ENA(stage->info.ps.spi_ps_input_ena) &&
                                  G_0286CC_PERSP_CENTROID_ENA(stage->info.ps.spi_ps_input_ena),
         .bc_optimize_for_linear = G_0286CC_LINEAR_CENTER_ENA(stage->info.ps.spi_ps_input_ena) &&
                                   G_0286CC_LINEAR_CENTROID_ENA(stage->info.ps.spi_ps_input_ena),
         .uses_discard = stage->info.ps.can_discard,
         .dcc_decompress_gfx11 = gfx_state->dcc_decompress_gfx11,
         .no_color_export = stage->info.ps.has_epilog,
         .no_depth_export = stage->info.ps.exports_mrtz_via_epilog,

      };

      if (!late_options.no_color_export) {
         late_options.dual_src_blend = gfx_state->ps.epilog.mrt0_is_dual_src;
         late_options.color_is_int8 = gfx_state->ps.epilog.color_is_int8;
         late_options.color_is_int10 = gfx_state->ps.epilog.color_is_int10;
         late_options.enable_mrt_output_nan_fixup =
            gfx_state->ps.epilog.enable_mrt_output_nan_fixup && !stage->nir->info.internal;
         /* Need to filter out unwritten color slots. */
         late_options.spi_shader_col_format =
            gfx_state->ps.epilog.spi_shader_col_format & stage->info.ps.colors_written;
         late_options.alpha_to_one = gfx_state->ps.epilog.alpha_to_one;
      }

      if (!late_options.no_depth_export) {
         late_options.alpha_to_coverage_via_mrtz = stage->info.ps.writes_mrt0_alpha;
      }

      NIR_PASS(_, stage->nir, ac_nir_lower_ps_late, &late_options);
   }

   if (radv_shader_should_clear_lds(compiler_info, stage->nir)) {
      const unsigned chunk_size = 16; /* max single store size */
      const unsigned shared_size = align(stage->nir->info.shared_size, chunk_size);
      NIR_PASS(_, stage->nir, nir_clear_shared_memory, shared_size, chunk_size);
   }

   /* This must be after lowering resources to descriptor loads and before lowering intrinsics
    * to args and lowering int64.
    */
   if (!use_llvm)
      ac_nir_optimize_uniform_atomics(stage->nir);

   NIR_PASS(_, stage->nir, nir_opt_uniform_subgroup,
            &(struct nir_lower_subgroups_options){
               .subgroup_size = stage->info.wave_size,
               .ballot_bit_size = stage->info.wave_size,
               .ballot_components = 1,
               .lower_ballot_bit_count_to_mbcnt_amd = true,
            });

   NIR_PASS(_, stage->nir, nir_opt_idiv_const, 8);

   NIR_PASS(_, stage->nir, nir_lower_idiv,
            &(nir_lower_idiv_options){
               .allow_fp16 = gfx_level >= GFX9,
            });

   NIR_PASS(
      _, stage->nir, ac_nir_lower_intrinsics_to_args, &stage->args.ac,
      &(ac_nir_lower_intrinsics_to_args_options){
         .gfx_level = gfx_level,
         .has_ls_vgpr_init_bug = compiler_info->ac->has_ls_vgpr_init_bug && gfx_state && !gfx_state->vs.has_prolog,
         .hw_stage = radv_select_hw_stage(&stage->info, gfx_level),
         .wave_size = stage->info.wave_size,
         .workgroup_size = stage->info.workgroup_size,
         .use_llvm = use_llvm,
         .load_grid_size_from_user_sgpr = compiler_info->key.load_grid_size_from_user_sgpr,
      });
   NIR_PASS(_, stage->nir, radv_nir_lower_abi, gfx_level, stage, gfx_state, compiler_info->hw.address32_hi);

   if (!stage->key.optimisations_disabled) {
      NIR_PASS(_, stage->nir, nir_opt_dce);

      NIR_PASS(_, stage->nir, nir_opt_copy_prop);
      NIR_PASS(_, stage->nir, nir_opt_constant_folding);
      NIR_PASS(_, stage->nir, nir_opt_cse);
      NIR_PASS(_, stage->nir, nir_opt_if, nir_opt_if_optimize_phi_true_false);
      NIR_PASS(_, stage->nir, nir_opt_shrink_vectors, true);

      NIR_PASS(_, stage->nir, ac_nir_flag_smem_for_loads, gfx_level, use_llvm);
      NIR_PASS(_, stage->nir, ac_nir_lower_mem_access_bit_sizes, gfx_level, use_llvm);

      nir_load_store_vectorize_options late_vectorize_opts = {
         .modes =
            nir_var_mem_global | nir_var_mem_shared | nir_var_shader_out | nir_var_mem_task_payload | nir_var_shader_in,
         .callback = ac_nir_mem_vectorize_callback,
         .cb_data = &(struct ac_nir_config){gfx_level, !use_llvm},
         .robust_modes = 0,
         .bounds_checked_modes = nir_var_mem_ssbo | nir_var_mem_ubo | nir_var_mem_shared,
         .has_shared2_amd = true,
      };
      NIR_PASS(_, stage->nir, nir_opt_load_store_vectorize, &late_vectorize_opts);
   }

   NIR_PASS(_, stage->nir, ac_nir_lower_mem_access_bit_sizes, gfx_level, use_llvm);
   NIR_PASS(_, stage->nir, ac_nir_lower_global_access);
   NIR_PASS(_, stage->nir, nir_lower_int64);

   if (compiler_info->key.mitigate_smem_with_null_prt)
      NIR_PASS(_, stage->nir, ac_nir_fixup_smem_loads_null_prt, compiler_info->hw.address_prt_wa_control_bit);

   if (compiler_info->key.mitigate_smem_oob)
      NIR_PASS(_, stage->nir, ac_nir_fixup_mem_access_gfx6, &stage->args.ac, 4096, true, true);

   bool opt_intrinsics = false;
   if (gfx_level >= GFX11)
      NIR_PASS(opt_intrinsics, stage->nir, ac_nir_opt_flip_if_for_mem_loads);
   if (opt_intrinsics) /* optimize inot(inverse_ballot) */
      NIR_PASS(_, stage->nir, nir_opt_intrinsics);

   NIR_PASS(_, stage->nir, nir_opt_uub, &(nir_opt_uub_options){0});

   radv_optimize_nir_algebraic(
      stage->nir, io_to_mem || lowered_ngg || stage->stage == MESA_SHADER_COMPUTE || stage->stage == MESA_SHADER_TASK,
      gfx_level >= GFX8, gfx_level);

   if (stage->nir->info.cs.has_cooperative_matrix)
      NIR_PASS(_, stage->nir, radv_nir_opt_cooperative_matrix, gfx_level);

   NIR_PASS(_, stage->nir, nir_lower_fp16_casts, nir_lower_fp16_split_fp64);

   if (ac_nir_might_lower_bit_size(stage->nir)) {
      if (gfx_level >= GFX8)
         nir_divergence_analysis(stage->nir);

      NIR_PASS(_, stage->nir, nir_lower_bit_size, ac_nir_lower_bit_size_callback, &gfx_level);
   }
   if (gfx_level >= GFX9) {
      bool separate_g16 = gfx_level >= GFX10;
      struct nir_opt_tex_srcs_options opt_srcs_options[] = {
         {
            .sampler_dims = ~(BITFIELD_BIT(GLSL_SAMPLER_DIM_CUBE) | BITFIELD_BIT(GLSL_SAMPLER_DIM_BUF)),
            .src_types = (1 << nir_tex_src_coord) | (1 << nir_tex_src_lod) | (1 << nir_tex_src_bias) |
                         (1 << nir_tex_src_min_lod) | (1 << nir_tex_src_ms_index) |
                         (separate_g16 ? 0 : (1 << nir_tex_src_ddx) | (1 << nir_tex_src_ddy)),
         },
         {
            .sampler_dims = ~BITFIELD_BIT(GLSL_SAMPLER_DIM_CUBE),
            .src_types = (1 << nir_tex_src_ddx) | (1 << nir_tex_src_ddy),
         },
      };
      struct nir_opt_16bit_tex_image_options opt_16bit_options = {
         .rounding_mode = nir_rounding_mode_undef,
         .opt_tex_dest_types = nir_type_float | nir_type_int | nir_type_uint,
         .opt_image_dest_types = nir_type_float | nir_type_int | nir_type_uint,
         .integer_dest_saturates = true,
         .opt_image_store_data = true,
         .opt_image_srcs = true,
         .opt_srcs_options_count = separate_g16 ? 2 : 1,
         .opt_srcs_options = opt_srcs_options,
      };
      bool run_copy_prop = false;
      NIR_PASS(run_copy_prop, stage->nir, nir_opt_16bit_tex_image, &opt_16bit_options);

      if (run_copy_prop) {
         NIR_PASS(_, stage->nir, nir_opt_copy_prop);
         NIR_PASS(_, stage->nir, nir_opt_dce);
      }

      if (!stage->key.optimisations_disabled) {
         NIR_PASS(_, stage->nir, nir_opt_vectorize, ac_nir_opt_vectorize_cb, &gfx_level);
      }
   }

   /* cleanup passes */
   NIR_PASS(_, stage->nir, nir_lower_alu_width, ac_nir_opt_vectorize_cb, &gfx_level);

   NIR_PASS(_, stage->nir, nir_lower_load_const_to_scalar);
   NIR_PASS(_, stage->nir, nir_opt_copy_prop);
   NIR_PASS(_, stage->nir, nir_opt_dce);

   if (!stage->key.optimisations_disabled) {
      sink_opts |= nir_move_comparisons | nir_move_load_ubo | nir_move_load_ssbo | nir_move_alu;
      NIR_PASS(_, stage->nir, nir_opt_sink, sink_opts);

      nir_move_options move_opts = nir_move_const_undef | nir_move_load_ubo | nir_move_load_input |
                                   nir_move_load_frag_coord | nir_move_comparisons | nir_move_copies |
                                   nir_dont_move_byte_word_vecs | nir_move_alu;
      NIR_PASS(_, stage->nir, nir_opt_move, move_opts);

      NIR_PASS(_, stage->nir, nir_opt_move, nir_move_comparisons);
   }

   stage->info.nir_shared_size = stage->nir->info.shared_size;
}

bool
radv_shader_should_clear_lds(const struct radv_compiler_info *compiler_info, const nir_shader *shader)
{
   return (shader->info.stage == MESA_SHADER_COMPUTE || shader->info.stage == MESA_SHADER_MESH ||
           shader->info.stage == MESA_SHADER_TASK) &&
          shader->info.shared_size > 0 && compiler_info->key.clear_lds;
}
