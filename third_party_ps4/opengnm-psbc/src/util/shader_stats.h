#ifndef __SHADER_STATS_H
#define __SHADER_STATS_H
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "util/u_debug.h"

struct adreno_stats {
   uint16_t maxwaves;
   uint32_t inst;
   uint32_t code_size;
   uint32_t nops;
   uint32_t mov;
   uint32_t cov;
   uint16_t full;
   uint16_t half;
   uint32_t last_baryf;
   uint32_t last_helper;
   uint32_t ss;
   uint32_t sy;
   uint32_t ss_stall;
   uint32_t sy_stall;
   uint32_t cat[8];
   uint32_t loops;
   uint32_t stps;
   uint32_t ldps;
   uint32_t preamble_inst;
   bool early_preamble;
   uint32_t constlen;
};

static inline int
adreno_stats_fprintf(FILE *fp, const char *prefix, const struct adreno_stats *stats)
{
   return fprintf(fp, "%s shader: %" PRIu16 " maxwaves, %" PRIu32 " inst, %" PRIu32 " code size, %" PRIu32 " nops, %" PRIu32 " mov, %" PRIu32 " cov, %" PRIu16 " full, %" PRIu16 " half, %" PRIu32 " last-baryf, %" PRIu32 " last-helper, %" PRIu32 " (ss), %" PRIu32 " (sy), %" PRIu32 " (ss)-stall, %" PRIu32 " (sy)-stall, %" PRIu32 " cat0, %" PRIu32 " cat1, %" PRIu32 " cat2, %" PRIu32 " cat3, %" PRIu32 " cat4, %" PRIu32 " cat5, %" PRIu32 " cat6, %" PRIu32 " cat7, %" PRIu32 " loops, %" PRIu32 " stps, %" PRIu32 " ldps, %" PRIu32 " preamble inst, %u early-preamble, %" PRIu32 " constlen\n", prefix, stats->maxwaves, stats->inst, stats->code_size, stats->nops, stats->mov, stats->cov, stats->full, stats->half, stats->last_baryf, stats->last_helper, stats->ss, stats->sy, stats->ss_stall, stats->sy_stall, stats->cat[0], stats->cat[1], stats->cat[2], stats->cat[3], stats->cat[4], stats->cat[5], stats->cat[6], stats->cat[7], stats->loops, stats->stps, stats->ldps, stats->preamble_inst, stats->early_preamble, stats->constlen);
}

static inline void
adreno_stats_util_debug(struct util_debug_callback *debug, const char *prefix, const struct adreno_stats *stats)
{
   util_debug_message(debug, SHADER_INFO, "%s shader: %" PRIu16 " maxwaves, %" PRIu32 " inst, %" PRIu32 " code size, %" PRIu32 " nops, %" PRIu32 " mov, %" PRIu32 " cov, %" PRIu16 " full, %" PRIu16 " half, %" PRIu32 " last-baryf, %" PRIu32 " last-helper, %" PRIu32 " (ss), %" PRIu32 " (sy), %" PRIu32 " (ss)-stall, %" PRIu32 " (sy)-stall, %" PRIu32 " cat0, %" PRIu32 " cat1, %" PRIu32 " cat2, %" PRIu32 " cat3, %" PRIu32 " cat4, %" PRIu32 " cat5, %" PRIu32 " cat6, %" PRIu32 " cat7, %" PRIu32 " loops, %" PRIu32 " stps, %" PRIu32 " ldps, %" PRIu32 " preamble inst, %u early-preamble, %" PRIu32 " constlen", prefix, stats->maxwaves, stats->inst, stats->code_size, stats->nops, stats->mov, stats->cov, stats->full, stats->half, stats->last_baryf, stats->last_helper, stats->ss, stats->sy, stats->ss_stall, stats->sy_stall, stats->cat[0], stats->cat[1], stats->cat[2], stats->cat[3], stats->cat[4], stats->cat[5], stats->cat[6], stats->cat[7], stats->loops, stats->stps, stats->ldps, stats->preamble_inst, stats->early_preamble, stats->constlen);
}

#define vk_add_adreno_stats(out, stats) do { \
   vk_add_exec_statistic_u64(out, "Max Waves Per Core", "Maximum number of simultaneous waves per core.", (stats)->maxwaves); \
   vk_add_exec_statistic_u64(out, "Instruction Count", "Total number of IR3 instructions in the final generated shader executable.", (stats)->inst); \
   vk_add_exec_statistic_u64(out, "Code size", "Total number of dwords in the final generated shader executable.", (stats)->code_size); \
   vk_add_exec_statistic_u64(out, "NOPs Count", "Number of NOP instructions in the final generated shader executable.", (stats)->nops); \
   vk_add_exec_statistic_u64(out, "MOV Count", "Number of MOV instructions in the final generated shader executable.", (stats)->mov); \
   vk_add_exec_statistic_u64(out, "COV Count", "Number of COV instructions in the final generated shader executable.", (stats)->cov); \
   vk_add_exec_statistic_u64(out, "Registers used", "Number of registers used in the final generated shader executable.", (stats)->full); \
   vk_add_exec_statistic_u64(out, "Half-registers used", "Number of half-registers used in the final generated shader executable.", (stats)->half); \
   vk_add_exec_statistic_u64(out, "Last interpolation instruction", "The instruction where varying storage in Local Memory is released", (stats)->last_baryf); \
   vk_add_exec_statistic_u64(out, "Last helper instruction", "The instruction where helper invocations are killed", (stats)->last_helper); \
   vk_add_exec_statistic_u64(out, "Instructions with SS sync bit", "SS bit is set for instructions which depend on a result of long instructions to prevent RAW hazard.", (stats)->ss); \
   vk_add_exec_statistic_u64(out, "Instructions with SY sync bit", "SY bit is set for instructions which depend on a result of loads from global memory to prevent RAW hazard.", (stats)->sy); \
   vk_add_exec_statistic_u64(out, "Estimated cycles stalled on SS", "A better metric to estimate the impact of SS syncs.", (stats)->ss_stall); \
   vk_add_exec_statistic_u64(out, "Estimated cycles stalled on SY", "A better metric to estimate the impact of SY syncs.", (stats)->sy_stall); \
   vk_add_exec_statistic_u64(out, "cat0 instructions", "Number of cat0 instructions.", (stats)->cat[0]); \
   vk_add_exec_statistic_u64(out, "cat1 instructions", "Number of cat1 instructions.", (stats)->cat[1]); \
   vk_add_exec_statistic_u64(out, "cat2 instructions", "Number of cat2 instructions.", (stats)->cat[2]); \
   vk_add_exec_statistic_u64(out, "cat3 instructions", "Number of cat3 instructions.", (stats)->cat[3]); \
   vk_add_exec_statistic_u64(out, "cat4 instructions", "Number of cat4 instructions.", (stats)->cat[4]); \
   vk_add_exec_statistic_u64(out, "cat5 instructions", "Number of cat5 instructions.", (stats)->cat[5]); \
   vk_add_exec_statistic_u64(out, "cat6 instructions", "Number of cat6 instructions.", (stats)->cat[6]); \
   vk_add_exec_statistic_u64(out, "cat7 instructions", "Number of cat7 instructions.", (stats)->cat[7]); \
   vk_add_exec_statistic_u64(out, "Loops", "Number of hardware loops", (stats)->loops); \
   vk_add_exec_statistic_u64(out, "STP Count", "Number of STore Private instructions in the final generated shader executable.", (stats)->stps); \
   vk_add_exec_statistic_u64(out, "LDP Count", "Number of LoaD Private instructions in the final generated shader executable.", (stats)->ldps); \
   vk_add_exec_statistic_u64(out, "Preamble Instruction Count", "Total number of IR3 instructions in the preamble.", (stats)->preamble_inst); \
   vk_add_exec_statistic_bool(out, "Early preamble", "Whether the preamble will be executed early.", (stats)->early_preamble); \
   vk_add_exec_statistic_u64(out, "Const file size", "Size of the const file in vec4", (stats)->constlen); \
} while(0)

static inline void
adreno_stats_serialize(uint8_t *dest, const struct adreno_stats *stats)
{
    memset(dest, 0, sizeof(*stats)); /* zero initialize any padding */
    memcpy(dest + offsetof(struct adreno_stats, maxwaves), &stats->maxwaves, sizeof(uint16_t));
    memcpy(dest + offsetof(struct adreno_stats, inst), &stats->inst, sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, code_size), &stats->code_size, sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, nops), &stats->nops, sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, mov), &stats->mov, sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, cov), &stats->cov, sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, full), &stats->full, sizeof(uint16_t));
    memcpy(dest + offsetof(struct adreno_stats, half), &stats->half, sizeof(uint16_t));
    memcpy(dest + offsetof(struct adreno_stats, last_baryf), &stats->last_baryf, sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, last_helper), &stats->last_helper, sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, ss), &stats->ss, sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, sy), &stats->sy, sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, ss_stall), &stats->ss_stall, sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, sy_stall), &stats->sy_stall, sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, cat) + 0 * sizeof(uint32_t), &stats->cat[0], sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, cat) + 1 * sizeof(uint32_t), &stats->cat[1], sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, cat) + 2 * sizeof(uint32_t), &stats->cat[2], sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, cat) + 3 * sizeof(uint32_t), &stats->cat[3], sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, cat) + 4 * sizeof(uint32_t), &stats->cat[4], sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, cat) + 5 * sizeof(uint32_t), &stats->cat[5], sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, cat) + 6 * sizeof(uint32_t), &stats->cat[6], sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, cat) + 7 * sizeof(uint32_t), &stats->cat[7], sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, loops), &stats->loops, sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, stps), &stats->stps, sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, ldps), &stats->ldps, sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, preamble_inst), &stats->preamble_inst, sizeof(uint32_t));
    memcpy(dest + offsetof(struct adreno_stats, early_preamble), &stats->early_preamble, sizeof(bool));
    memcpy(dest + offsetof(struct adreno_stats, constlen), &stats->constlen, sizeof(uint32_t));
}

struct agx2_stats {
   uint32_t instrs;
   uint32_t alu;
   uint32_t fscib;
   uint32_t ic;
   uint32_t code_size;
   uint16_t gprs;
   uint16_t uniforms;
   uint32_t scratch;
   uint16_t threads;
   uint32_t loops;
   uint32_t spills;
   uint32_t fills;
   uint32_t preamble_inst;
};

static inline int
agx2_stats_fprintf(FILE *fp, const char *prefix, const struct agx2_stats *stats)
{
   return fprintf(fp, "%s shader: %" PRIu32 " instrs, %" PRIu32 " alu, %" PRIu32 " fscib, %" PRIu32 " ic, %" PRIu32 " code size, %" PRIu16 " gprs, %" PRIu16 " uniforms, %" PRIu32 " scratch, %" PRIu16 " threads, %" PRIu32 " loops, %" PRIu32 ":%" PRIu32 " spills:fills, %" PRIu32 " preamble inst\n", prefix, stats->instrs, stats->alu, stats->fscib, stats->ic, stats->code_size, stats->gprs, stats->uniforms, stats->scratch, stats->threads, stats->loops, stats->spills, stats->fills, stats->preamble_inst);
}

static inline void
agx2_stats_util_debug(struct util_debug_callback *debug, const char *prefix, const struct agx2_stats *stats)
{
   util_debug_message(debug, SHADER_INFO, "%s shader: %" PRIu32 " instrs, %" PRIu32 " alu, %" PRIu32 " fscib, %" PRIu32 " ic, %" PRIu32 " code size, %" PRIu16 " gprs, %" PRIu16 " uniforms, %" PRIu32 " scratch, %" PRIu16 " threads, %" PRIu32 " loops, %" PRIu32 ":%" PRIu32 " spills:fills, %" PRIu32 " preamble inst", prefix, stats->instrs, stats->alu, stats->fscib, stats->ic, stats->code_size, stats->gprs, stats->uniforms, stats->scratch, stats->threads, stats->loops, stats->spills, stats->fills, stats->preamble_inst);
}

#define vk_add_agx2_stats(out, stats) do { \
   vk_add_exec_statistic_u64(out, "Instructions", "Instruction count", (stats)->instrs); \
   vk_add_exec_statistic_u64(out, "ALU", "Estimated ALU cycle count", (stats)->alu); \
   vk_add_exec_statistic_u64(out, "FSCIB", "Estimated F16/F32/SCIB cycle count", (stats)->fscib); \
   vk_add_exec_statistic_u64(out, "IC", "Estimated IC cycle count", (stats)->ic); \
   vk_add_exec_statistic_u64(out, "Code size", "Binary size in bytes", (stats)->code_size); \
   vk_add_exec_statistic_u64(out, "GPRs", "Number of 16-bit GPRs", (stats)->gprs); \
   vk_add_exec_statistic_u64(out, "Uniforms", "Number of 16-bit uniform registers", (stats)->uniforms); \
   vk_add_exec_statistic_u64(out, "Scratch", "Scratch size per thread in bytes", (stats)->scratch); \
   vk_add_exec_statistic_u64(out, "Threads", "Maximum number of threads in flight on a compute unit", (stats)->threads); \
   vk_add_exec_statistic_u64(out, "Loops", "Number of hardware loops", (stats)->loops); \
   vk_add_exec_statistic_u64(out, "Spills", "Number of spill (stack store) instructions", (stats)->spills); \
   vk_add_exec_statistic_u64(out, "Fills", "Number of fill (stack load) instructions", (stats)->fills); \
   vk_add_exec_statistic_u64(out, "Preamble instructions", "Preamble instruction count", (stats)->preamble_inst); \
} while(0)

static inline void
agx2_stats_serialize(uint8_t *dest, const struct agx2_stats *stats)
{
    memset(dest, 0, sizeof(*stats)); /* zero initialize any padding */
    memcpy(dest + offsetof(struct agx2_stats, instrs), &stats->instrs, sizeof(uint32_t));
    memcpy(dest + offsetof(struct agx2_stats, alu), &stats->alu, sizeof(uint32_t));
    memcpy(dest + offsetof(struct agx2_stats, fscib), &stats->fscib, sizeof(uint32_t));
    memcpy(dest + offsetof(struct agx2_stats, ic), &stats->ic, sizeof(uint32_t));
    memcpy(dest + offsetof(struct agx2_stats, code_size), &stats->code_size, sizeof(uint32_t));
    memcpy(dest + offsetof(struct agx2_stats, gprs), &stats->gprs, sizeof(uint16_t));
    memcpy(dest + offsetof(struct agx2_stats, uniforms), &stats->uniforms, sizeof(uint16_t));
    memcpy(dest + offsetof(struct agx2_stats, scratch), &stats->scratch, sizeof(uint32_t));
    memcpy(dest + offsetof(struct agx2_stats, threads), &stats->threads, sizeof(uint16_t));
    memcpy(dest + offsetof(struct agx2_stats, loops), &stats->loops, sizeof(uint32_t));
    memcpy(dest + offsetof(struct agx2_stats, spills), &stats->spills, sizeof(uint32_t));
    memcpy(dest + offsetof(struct agx2_stats, fills), &stats->fills, sizeof(uint32_t));
    memcpy(dest + offsetof(struct agx2_stats, preamble_inst), &stats->preamble_inst, sizeof(uint32_t));
}

struct midgard_stats {
   uint32_t inst;
   uint32_t bundles;
   uint16_t registers;
   uint16_t threads;
   uint32_t quadwords;
   uint32_t loops;
   uint32_t spills;
   uint32_t fills;
};

static inline int
midgard_stats_fprintf(FILE *fp, const char *prefix, const struct midgard_stats *stats)
{
   return fprintf(fp, "%s shader: %" PRIu32 " inst, %" PRIu32 " bundles, %" PRIu16 " registers, %" PRIu16 " threads, %" PRIu32 " quadwords, %" PRIu32 " loops, %" PRIu32 ":%" PRIu32 " spills:fills\n", prefix, stats->inst, stats->bundles, stats->registers, stats->threads, stats->quadwords, stats->loops, stats->spills, stats->fills);
}

static inline void
midgard_stats_util_debug(struct util_debug_callback *debug, const char *prefix, const struct midgard_stats *stats)
{
   util_debug_message(debug, SHADER_INFO, "%s shader: %" PRIu32 " inst, %" PRIu32 " bundles, %" PRIu16 " registers, %" PRIu16 " threads, %" PRIu32 " quadwords, %" PRIu32 " loops, %" PRIu32 ":%" PRIu32 " spills:fills", prefix, stats->inst, stats->bundles, stats->registers, stats->threads, stats->quadwords, stats->loops, stats->spills, stats->fills);
}

#define vk_add_midgard_stats(out, stats) do { \
   vk_add_exec_statistic_u64(out, "Instructions", "Instruction count", (stats)->inst); \
   vk_add_exec_statistic_u64(out, "Bundles", "Instruction bundles", (stats)->bundles); \
   vk_add_exec_statistic_u64(out, "Registers", "Register usage in vec4s", (stats)->registers); \
   vk_add_exec_statistic_u64(out, "Threads", "Maximum number of threads in flight on a compute unit", (stats)->threads); \
   vk_add_exec_statistic_u64(out, "Quadwords", "Binary size in quadwords", (stats)->quadwords); \
   vk_add_exec_statistic_u64(out, "Loops", "Number of hardware loops", (stats)->loops); \
   vk_add_exec_statistic_u64(out, "Spills", "Number of spill instructions", (stats)->spills); \
   vk_add_exec_statistic_u64(out, "Fills", "Number of fill instructions", (stats)->fills); \
} while(0)

static inline void
midgard_stats_serialize(uint8_t *dest, const struct midgard_stats *stats)
{
    memset(dest, 0, sizeof(*stats)); /* zero initialize any padding */
    memcpy(dest + offsetof(struct midgard_stats, inst), &stats->inst, sizeof(uint32_t));
    memcpy(dest + offsetof(struct midgard_stats, bundles), &stats->bundles, sizeof(uint32_t));
    memcpy(dest + offsetof(struct midgard_stats, registers), &stats->registers, sizeof(uint16_t));
    memcpy(dest + offsetof(struct midgard_stats, threads), &stats->threads, sizeof(uint16_t));
    memcpy(dest + offsetof(struct midgard_stats, quadwords), &stats->quadwords, sizeof(uint32_t));
    memcpy(dest + offsetof(struct midgard_stats, loops), &stats->loops, sizeof(uint32_t));
    memcpy(dest + offsetof(struct midgard_stats, spills), &stats->spills, sizeof(uint32_t));
    memcpy(dest + offsetof(struct midgard_stats, fills), &stats->fills, sizeof(uint32_t));
}

struct bifrost_stats {
   uint32_t instrs;
   uint32_t tuples;
   uint32_t clauses;
   float cycles;
   float arith;
   float t;
   float ldst;
   float v;
   uint16_t preloads;
   uint16_t threads;
   uint32_t code_size;
   uint32_t loops;
   uint32_t spills;
   uint32_t fills;
   uint32_t spill_cost;
   uint32_t registers_used;
   uint32_t uniforms_used;
};

static inline int
bifrost_stats_fprintf(FILE *fp, const char *prefix, const struct bifrost_stats *stats)
{
   return fprintf(fp, "%s shader: %" PRIu32 " instrs, %" PRIu32 " tuples, %" PRIu32 " clauses, %f cycles, %f arith, %f t, %f ldst, %f v, %" PRIu16 " preloads, %" PRIu16 " threads, %" PRIu32 " code size, %" PRIu32 " loops, %" PRIu32 ":%" PRIu32 " spills:fills, %" PRIu32 " spill cost, %" PRIu32 " registers used, %" PRIu32 " uniforms used\n", prefix, stats->instrs, stats->tuples, stats->clauses, stats->cycles, stats->arith, stats->t, stats->ldst, stats->v, stats->preloads, stats->threads, stats->code_size, stats->loops, stats->spills, stats->fills, stats->spill_cost, stats->registers_used, stats->uniforms_used);
}

static inline void
bifrost_stats_util_debug(struct util_debug_callback *debug, const char *prefix, const struct bifrost_stats *stats)
{
   util_debug_message(debug, SHADER_INFO, "%s shader: %" PRIu32 " instrs, %" PRIu32 " tuples, %" PRIu32 " clauses, %f cycles, %f arith, %f t, %f ldst, %f v, %" PRIu16 " preloads, %" PRIu16 " threads, %" PRIu32 " code size, %" PRIu32 " loops, %" PRIu32 ":%" PRIu32 " spills:fills, %" PRIu32 " spill cost, %" PRIu32 " registers used, %" PRIu32 " uniforms used", prefix, stats->instrs, stats->tuples, stats->clauses, stats->cycles, stats->arith, stats->t, stats->ldst, stats->v, stats->preloads, stats->threads, stats->code_size, stats->loops, stats->spills, stats->fills, stats->spill_cost, stats->registers_used, stats->uniforms_used);
}

#define vk_add_bifrost_stats(out, stats) do { \
   vk_add_exec_statistic_u64(out, "Instructions", "Instruction count", (stats)->instrs); \
   vk_add_exec_statistic_u64(out, "Tuples", "Tuple count", (stats)->tuples); \
   vk_add_exec_statistic_u64(out, "Clauses", "Clause count", (stats)->clauses); \
   vk_add_exec_statistic_f64(out, "Cycles", "Estimated normalized cycles", (stats)->cycles); \
   vk_add_exec_statistic_f64(out, "Arithmetic", "Estimated normalized arithmetic cycles", (stats)->arith); \
   vk_add_exec_statistic_f64(out, "Texture", "Estimated normalized Texture cycles", (stats)->t); \
   vk_add_exec_statistic_f64(out, "Load/store", "Estimated normalized Load/Store cycles", (stats)->ldst); \
   vk_add_exec_statistic_f64(out, "Varying", "Estimated normalized Varying cycles", (stats)->v); \
   vk_add_exec_statistic_u64(out, "Preloads", "Preload count", (stats)->preloads); \
   vk_add_exec_statistic_u64(out, "Threads", "Maximum number of threads in flight on a compute unit", (stats)->threads); \
   vk_add_exec_statistic_u64(out, "Code size", "Binary size in bytes", (stats)->code_size); \
   vk_add_exec_statistic_u64(out, "Loops", "Number of hardware loops", (stats)->loops); \
   vk_add_exec_statistic_u64(out, "Spills", "Number of spill instructions", (stats)->spills); \
   vk_add_exec_statistic_u64(out, "Fills", "Number of fill instructions", (stats)->fills); \
   vk_add_exec_statistic_u64(out, "Spill cost", "Cost of spill and fill instructions", (stats)->spill_cost); \
   vk_add_exec_statistic_u64(out, "Registers used", "Number of registers used", (stats)->registers_used); \
   vk_add_exec_statistic_u64(out, "Uniforms used", "Uniform registers used", (stats)->uniforms_used); \
} while(0)

static inline void
bifrost_stats_serialize(uint8_t *dest, const struct bifrost_stats *stats)
{
    memset(dest, 0, sizeof(*stats)); /* zero initialize any padding */
    memcpy(dest + offsetof(struct bifrost_stats, instrs), &stats->instrs, sizeof(uint32_t));
    memcpy(dest + offsetof(struct bifrost_stats, tuples), &stats->tuples, sizeof(uint32_t));
    memcpy(dest + offsetof(struct bifrost_stats, clauses), &stats->clauses, sizeof(uint32_t));
    memcpy(dest + offsetof(struct bifrost_stats, cycles), &stats->cycles, sizeof(float));
    memcpy(dest + offsetof(struct bifrost_stats, arith), &stats->arith, sizeof(float));
    memcpy(dest + offsetof(struct bifrost_stats, t), &stats->t, sizeof(float));
    memcpy(dest + offsetof(struct bifrost_stats, ldst), &stats->ldst, sizeof(float));
    memcpy(dest + offsetof(struct bifrost_stats, v), &stats->v, sizeof(float));
    memcpy(dest + offsetof(struct bifrost_stats, preloads), &stats->preloads, sizeof(uint16_t));
    memcpy(dest + offsetof(struct bifrost_stats, threads), &stats->threads, sizeof(uint16_t));
    memcpy(dest + offsetof(struct bifrost_stats, code_size), &stats->code_size, sizeof(uint32_t));
    memcpy(dest + offsetof(struct bifrost_stats, loops), &stats->loops, sizeof(uint32_t));
    memcpy(dest + offsetof(struct bifrost_stats, spills), &stats->spills, sizeof(uint32_t));
    memcpy(dest + offsetof(struct bifrost_stats, fills), &stats->fills, sizeof(uint32_t));
    memcpy(dest + offsetof(struct bifrost_stats, spill_cost), &stats->spill_cost, sizeof(uint32_t));
    memcpy(dest + offsetof(struct bifrost_stats, registers_used), &stats->registers_used, sizeof(uint32_t));
    memcpy(dest + offsetof(struct bifrost_stats, uniforms_used), &stats->uniforms_used, sizeof(uint32_t));
}

struct valhall_stats {
   uint32_t instrs;
   float cycles;
   float fma;
   float cvt;
   float sfu;
   float v;
   float t;
   float ls;
   uint32_t code_size;
   uint16_t threads;
   uint32_t loops;
   uint32_t spills;
   uint32_t fills;
   uint32_t spill_cost;
   uint32_t registers_used;
   uint32_t uniforms_used;
};

static inline int
valhall_stats_fprintf(FILE *fp, const char *prefix, const struct valhall_stats *stats)
{
   return fprintf(fp, "%s shader: %" PRIu32 " instrs, %f cycles, %f fma, %f cvt, %f sfu, %f v, %f t, %f ls, %" PRIu32 " code size, %" PRIu16 " threads, %" PRIu32 " loops, %" PRIu32 ":%" PRIu32 " spills:fills, %" PRIu32 " spill cost, %" PRIu32 " registers used, %" PRIu32 " uniforms used\n", prefix, stats->instrs, stats->cycles, stats->fma, stats->cvt, stats->sfu, stats->v, stats->t, stats->ls, stats->code_size, stats->threads, stats->loops, stats->spills, stats->fills, stats->spill_cost, stats->registers_used, stats->uniforms_used);
}

static inline void
valhall_stats_util_debug(struct util_debug_callback *debug, const char *prefix, const struct valhall_stats *stats)
{
   util_debug_message(debug, SHADER_INFO, "%s shader: %" PRIu32 " instrs, %f cycles, %f fma, %f cvt, %f sfu, %f v, %f t, %f ls, %" PRIu32 " code size, %" PRIu16 " threads, %" PRIu32 " loops, %" PRIu32 ":%" PRIu32 " spills:fills, %" PRIu32 " spill cost, %" PRIu32 " registers used, %" PRIu32 " uniforms used", prefix, stats->instrs, stats->cycles, stats->fma, stats->cvt, stats->sfu, stats->v, stats->t, stats->ls, stats->code_size, stats->threads, stats->loops, stats->spills, stats->fills, stats->spill_cost, stats->registers_used, stats->uniforms_used);
}

#define vk_add_valhall_stats(out, stats) do { \
   vk_add_exec_statistic_u64(out, "Instructions", "Instruction count", (stats)->instrs); \
   vk_add_exec_statistic_f64(out, "Cycles", "Estimated normalized cycles", (stats)->cycles); \
   vk_add_exec_statistic_f64(out, "FMA", "Estimated normalized FMA (Fused Multiply-Add) cycles", (stats)->fma); \
   vk_add_exec_statistic_f64(out, "CVT", "Estimated normalized CVT (ConVerT) cycles", (stats)->cvt); \
   vk_add_exec_statistic_f64(out, "SFU", "Estimated normalized SFU (Special Function Unit) cycles", (stats)->sfu); \
   vk_add_exec_statistic_f64(out, "Varying", "Estimated normalized Varying cycles", (stats)->v); \
   vk_add_exec_statistic_f64(out, "Texture", "Estimated normalized Texture cycles", (stats)->t); \
   vk_add_exec_statistic_f64(out, "Load/store", "Estimated normalized Load/Store cycles", (stats)->ls); \
   vk_add_exec_statistic_u64(out, "Code size", "Binary size in bytes", (stats)->code_size); \
   vk_add_exec_statistic_u64(out, "Threads", "Maximum number of threads in flight on a compute unit", (stats)->threads); \
   vk_add_exec_statistic_u64(out, "Loops", "Number of hardware loops", (stats)->loops); \
   vk_add_exec_statistic_u64(out, "Spills", "Number of spill instructions", (stats)->spills); \
   vk_add_exec_statistic_u64(out, "Fills", "Number of fill instructions", (stats)->fills); \
   vk_add_exec_statistic_u64(out, "Spill cost", "Cost of spill and fill instructions", (stats)->spill_cost); \
   vk_add_exec_statistic_u64(out, "Registers used", "Number of registers used", (stats)->registers_used); \
   vk_add_exec_statistic_u64(out, "Uniforms used", "Uniform registers used", (stats)->uniforms_used); \
} while(0)

static inline void
valhall_stats_serialize(uint8_t *dest, const struct valhall_stats *stats)
{
    memset(dest, 0, sizeof(*stats)); /* zero initialize any padding */
    memcpy(dest + offsetof(struct valhall_stats, instrs), &stats->instrs, sizeof(uint32_t));
    memcpy(dest + offsetof(struct valhall_stats, cycles), &stats->cycles, sizeof(float));
    memcpy(dest + offsetof(struct valhall_stats, fma), &stats->fma, sizeof(float));
    memcpy(dest + offsetof(struct valhall_stats, cvt), &stats->cvt, sizeof(float));
    memcpy(dest + offsetof(struct valhall_stats, sfu), &stats->sfu, sizeof(float));
    memcpy(dest + offsetof(struct valhall_stats, v), &stats->v, sizeof(float));
    memcpy(dest + offsetof(struct valhall_stats, t), &stats->t, sizeof(float));
    memcpy(dest + offsetof(struct valhall_stats, ls), &stats->ls, sizeof(float));
    memcpy(dest + offsetof(struct valhall_stats, code_size), &stats->code_size, sizeof(uint32_t));
    memcpy(dest + offsetof(struct valhall_stats, threads), &stats->threads, sizeof(uint16_t));
    memcpy(dest + offsetof(struct valhall_stats, loops), &stats->loops, sizeof(uint32_t));
    memcpy(dest + offsetof(struct valhall_stats, spills), &stats->spills, sizeof(uint32_t));
    memcpy(dest + offsetof(struct valhall_stats, fills), &stats->fills, sizeof(uint32_t));
    memcpy(dest + offsetof(struct valhall_stats, spill_cost), &stats->spill_cost, sizeof(uint32_t));
    memcpy(dest + offsetof(struct valhall_stats, registers_used), &stats->registers_used, sizeof(uint32_t));
    memcpy(dest + offsetof(struct valhall_stats, uniforms_used), &stats->uniforms_used, sizeof(uint32_t));
}

struct videocore_vi_stats {
   uint32_t instrs;
   uint32_t thread_count;
   uint32_t spill_size;
   uint32_t spills;
   uint32_t fills;
   uint32_t read_stalls;
};

static inline int
videocore_vi_stats_fprintf(FILE *fp, const char *prefix, const struct videocore_vi_stats *stats)
{
   return fprintf(fp, "%s shader: %" PRIu32 " instrs, %" PRIu32 " thread count, %" PRIu32 " spill size, %" PRIu32 ":%" PRIu32 " spills:fills, %" PRIu32 " read stalls\n", prefix, stats->instrs, stats->thread_count, stats->spill_size, stats->spills, stats->fills, stats->read_stalls);
}

static inline void
videocore_vi_stats_util_debug(struct util_debug_callback *debug, const char *prefix, const struct videocore_vi_stats *stats)
{
   util_debug_message(debug, SHADER_INFO, "%s shader: %" PRIu32 " instrs, %" PRIu32 " thread count, %" PRIu32 " spill size, %" PRIu32 ":%" PRIu32 " spills:fills, %" PRIu32 " read stalls", prefix, stats->instrs, stats->thread_count, stats->spill_size, stats->spills, stats->fills, stats->read_stalls);
}

#define vk_add_videocore_vi_stats(out, stats) do { \
   vk_add_exec_statistic_u64(out, "Instruction Count", "Number of QPU instructions", (stats)->instrs); \
   vk_add_exec_statistic_u64(out, "Thread Count", "Number of QPU threads dispatched", (stats)->thread_count); \
   vk_add_exec_statistic_u64(out, "Spill Size", "Size of the spill buffer in bytes", (stats)->spill_size); \
   vk_add_exec_statistic_u64(out, "TMU Spills", "Number of times a register was spilled to memory", (stats)->spills); \
   vk_add_exec_statistic_u64(out, "TMU Fills", "Number of times a register was filled from memory", (stats)->fills); \
   vk_add_exec_statistic_u64(out, "QPU Read Stalls", "Number of cycles the QPU stalls for a register read dependency", (stats)->read_stalls); \
} while(0)

static inline void
videocore_vi_stats_serialize(uint8_t *dest, const struct videocore_vi_stats *stats)
{
    memset(dest, 0, sizeof(*stats)); /* zero initialize any padding */
    memcpy(dest + offsetof(struct videocore_vi_stats, instrs), &stats->instrs, sizeof(uint32_t));
    memcpy(dest + offsetof(struct videocore_vi_stats, thread_count), &stats->thread_count, sizeof(uint32_t));
    memcpy(dest + offsetof(struct videocore_vi_stats, spill_size), &stats->spill_size, sizeof(uint32_t));
    memcpy(dest + offsetof(struct videocore_vi_stats, spills), &stats->spills, sizeof(uint32_t));
    memcpy(dest + offsetof(struct videocore_vi_stats, fills), &stats->fills, sizeof(uint32_t));
    memcpy(dest + offsetof(struct videocore_vi_stats, read_stalls), &stats->read_stalls, sizeof(uint32_t));
}

struct amd_stats {
   uint64_t driverhash;
   uint32_t sgprs;
   uint32_t vgprs;
   uint32_t spillsgprs;
   uint32_t spillvgprs;
   uint32_t codesize;
   uint32_t lds;
   uint32_t scratch;
   uint32_t maxwaves;
   uint32_t inputs;
   uint32_t outputs;
   uint32_t hash;
   uint32_t instrs;
   uint32_t copies;
   uint32_t branches;
   uint32_t latency;
   uint32_t invthroughput;
   uint32_t vclause;
   uint32_t sclause;
   uint32_t presgprs;
   uint32_t prevgprs;
   uint32_t valu;
   uint32_t salu;
   uint32_t vmem;
   uint32_t smem;
   uint32_t vopd;
};

static inline int
amd_stats_fprintf(FILE *fp, const char *prefix, const struct amd_stats *stats)
{
   return fprintf(fp, "%s shader: %" PRIu32 " sgprs, %" PRIu32 " vgprs, %" PRIu32 " spillsgprs, %" PRIu32 " spillvgprs, %" PRIu32 " codesize, %" PRIu32 " lds, %" PRIu32 " scratch, %" PRIu32 " maxwaves, %" PRIu32 " inputs, %" PRIu32 " outputs, %" PRIu32 " instrs, %" PRIu32 " copies, %" PRIu32 " branches, %" PRIu32 " latency, %" PRIu32 " invthroughput, %" PRIu32 " vclause, %" PRIu32 " sclause, %" PRIu32 " presgprs, %" PRIu32 " prevgprs, %" PRIu32 " valu, %" PRIu32 " salu, %" PRIu32 " vmem, %" PRIu32 " smem, %" PRIu32 " vopd\n", prefix, stats->sgprs, stats->vgprs, stats->spillsgprs, stats->spillvgprs, stats->codesize, stats->lds, stats->scratch, stats->maxwaves, stats->inputs, stats->outputs, stats->instrs, stats->copies, stats->branches, stats->latency, stats->invthroughput, stats->vclause, stats->sclause, stats->presgprs, stats->prevgprs, stats->valu, stats->salu, stats->vmem, stats->smem, stats->vopd);
}

static inline void
amd_stats_util_debug(struct util_debug_callback *debug, const char *prefix, const struct amd_stats *stats)
{
   util_debug_message(debug, SHADER_INFO, "%s shader: %" PRIu32 " sgprs, %" PRIu32 " vgprs, %" PRIu32 " spillsgprs, %" PRIu32 " spillvgprs, %" PRIu32 " codesize, %" PRIu32 " lds, %" PRIu32 " scratch, %" PRIu32 " maxwaves, %" PRIu32 " inputs, %" PRIu32 " outputs, %" PRIu32 " instrs, %" PRIu32 " copies, %" PRIu32 " branches, %" PRIu32 " latency, %" PRIu32 " invthroughput, %" PRIu32 " vclause, %" PRIu32 " sclause, %" PRIu32 " presgprs, %" PRIu32 " prevgprs, %" PRIu32 " valu, %" PRIu32 " salu, %" PRIu32 " vmem, %" PRIu32 " smem, %" PRIu32 " vopd", prefix, stats->sgprs, stats->vgprs, stats->spillsgprs, stats->spillvgprs, stats->codesize, stats->lds, stats->scratch, stats->maxwaves, stats->inputs, stats->outputs, stats->instrs, stats->copies, stats->branches, stats->latency, stats->invthroughput, stats->vclause, stats->sclause, stats->presgprs, stats->prevgprs, stats->valu, stats->salu, stats->vmem, stats->smem, stats->vopd);
}

#define vk_add_amd_stats(out, stats) do { \
   vk_add_exec_statistic_u64(out, "Driver pipeline hash", "Driver pipeline hash used by RGP", (stats)->driverhash); \
   vk_add_exec_statistic_u64(out, "SGPRs", "Number of SGPR registers allocated per subgroup", (stats)->sgprs); \
   vk_add_exec_statistic_u64(out, "VGPRs", "Number of VGPR registers allocated per subgroup", (stats)->vgprs); \
   vk_add_exec_statistic_u64(out, "Spilled SGPRs", "Number of SGPR registers spilled per subgroup", (stats)->spillsgprs); \
   vk_add_exec_statistic_u64(out, "Spilled VGPRs", "Number of VGPR registers spilled per subgroup", (stats)->spillvgprs); \
   vk_add_exec_statistic_u64(out, "Code size", "Code size in bytes", (stats)->codesize); \
   vk_add_exec_statistic_u64(out, "LDS size", "LDS size in bytes per workgroup", (stats)->lds); \
   vk_add_exec_statistic_u64(out, "Scratch size", "Private memory in bytes per subgroup", (stats)->scratch); \
   vk_add_exec_statistic_u64(out, "Subgroups per SIMD", "The maximum number of subgroups in flight on a SIMD unit", (stats)->maxwaves); \
   vk_add_exec_statistic_u64(out, "Combined inputs", "Number of input slots reserved for the shader (including merged stages)", (stats)->inputs); \
   vk_add_exec_statistic_u64(out, "Combined outputs", "Number of output slots reserved for the shader (including merged stages)", (stats)->outputs); \
   vk_add_exec_statistic_u64(out, "Hash", "CRC32 hash of code and constant data", (stats)->hash); \
   vk_add_exec_statistic_u64(out, "Instructions", "Instruction count", (stats)->instrs); \
   vk_add_exec_statistic_u64(out, "Copies", "Copy instructions created for pseudo-instructions", (stats)->copies); \
   vk_add_exec_statistic_u64(out, "Branches", "Branch instructions", (stats)->branches); \
   vk_add_exec_statistic_u64(out, "Latency", "Issue cycles plus stall cycles", (stats)->latency); \
   vk_add_exec_statistic_u64(out, "Inverse Throughput", "Estimated busy cycles to execute one wave", (stats)->invthroughput); \
   vk_add_exec_statistic_u64(out, "VMEM Clause", "Number of VMEM clauses (includes 1-sized clauses)", (stats)->vclause); \
   vk_add_exec_statistic_u64(out, "SMEM Clause", "Number of SMEM clauses (includes 1-sized clauses)", (stats)->sclause); \
   vk_add_exec_statistic_u64(out, "Pre-Sched SGPRs", "SGPR usage before scheduling", (stats)->presgprs); \
   vk_add_exec_statistic_u64(out, "Pre-Sched VGPRs", "VGPR usage before scheduling", (stats)->prevgprs); \
   vk_add_exec_statistic_u64(out, "VALU", "Number of VALU instructions", (stats)->valu); \
   vk_add_exec_statistic_u64(out, "SALU", "Number of SALU instructions", (stats)->salu); \
   vk_add_exec_statistic_u64(out, "VMEM", "Number of VMEM instructions", (stats)->vmem); \
   vk_add_exec_statistic_u64(out, "SMEM", "Number of SMEM instructions", (stats)->smem); \
   vk_add_exec_statistic_u64(out, "VOPD", "Number of VOPD instructions", (stats)->vopd); \
} while(0)

static inline void
amd_stats_serialize(uint8_t *dest, const struct amd_stats *stats)
{
    memset(dest, 0, sizeof(*stats)); /* zero initialize any padding */
    memcpy(dest + offsetof(struct amd_stats, driverhash), &stats->driverhash, sizeof(uint64_t));
    memcpy(dest + offsetof(struct amd_stats, sgprs), &stats->sgprs, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, vgprs), &stats->vgprs, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, spillsgprs), &stats->spillsgprs, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, spillvgprs), &stats->spillvgprs, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, codesize), &stats->codesize, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, lds), &stats->lds, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, scratch), &stats->scratch, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, maxwaves), &stats->maxwaves, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, inputs), &stats->inputs, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, outputs), &stats->outputs, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, hash), &stats->hash, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, instrs), &stats->instrs, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, copies), &stats->copies, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, branches), &stats->branches, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, latency), &stats->latency, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, invthroughput), &stats->invthroughput, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, vclause), &stats->vclause, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, sclause), &stats->sclause, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, presgprs), &stats->presgprs, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, prevgprs), &stats->prevgprs, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, valu), &stats->valu, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, salu), &stats->salu, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, vmem), &stats->vmem, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, smem), &stats->smem, sizeof(uint32_t));
    memcpy(dest + offsetof(struct amd_stats, vopd), &stats->vopd, sizeof(uint32_t));
}

struct genisa_stats {
   uint32_t dispatch_width;
   uint32_t max_polygons;
   uint32_t instrs;
   uint32_t code_size;
   uint32_t sends;
   uint32_t loops;
   uint32_t cycles;
   uint32_t spills;
   uint32_t fills;
   uint32_t scratch_memory_size;
   uint32_t grf_registers;
   uint32_t push_constant_ranges;
   uint32_t push_constant_registers;
   uint32_t max_dispatch_width;
   uint32_t max_live_registers;
   uint32_t workgroup_memory_size;
   uint32_t non_ssa_regs_after_nir;
   uint64_t source_hash;
   const char * scheduler_mode;
};

static inline int
genisa_stats_fprintf(FILE *fp, const char *prefix, const struct genisa_stats *stats)
{
   return fprintf(fp, "%s shader: %" PRIu32 " instrs, %" PRIu32 " code size, %" PRIu32 " sends, %" PRIu32 " loops, %" PRIu32 " cycles, %" PRIu32 ":%" PRIu32 " spills:fills, %" PRIu32 " scratch memory size, %" PRIu32 " grf registers, %" PRIu32 " push constant ranges, %" PRIu32 " push constant registers, %" PRIu32 " max dispatch width, %" PRIu32 " max live registers, %" PRIu32 " workgroup memory size, %" PRIu32 " non ssa regs after nir, %s scheduler mode\n", prefix, stats->instrs, stats->code_size, stats->sends, stats->loops, stats->cycles, stats->spills, stats->fills, stats->scratch_memory_size, stats->grf_registers, stats->push_constant_ranges, stats->push_constant_registers, stats->max_dispatch_width, stats->max_live_registers, stats->workgroup_memory_size, stats->non_ssa_regs_after_nir, stats->scheduler_mode);
}

static inline void
genisa_stats_util_debug(struct util_debug_callback *debug, const char *prefix, const struct genisa_stats *stats)
{
   util_debug_message(debug, SHADER_INFO, "%s shader: %" PRIu32 " instrs, %" PRIu32 " code size, %" PRIu32 " sends, %" PRIu32 " loops, %" PRIu32 " cycles, %" PRIu32 ":%" PRIu32 " spills:fills, %" PRIu32 " scratch memory size, %" PRIu32 " grf registers, %" PRIu32 " push constant ranges, %" PRIu32 " push constant registers, %" PRIu32 " max dispatch width, %" PRIu32 " max live registers, %" PRIu32 " workgroup memory size, %" PRIu32 " non ssa regs after nir, %s scheduler mode", prefix, stats->instrs, stats->code_size, stats->sends, stats->loops, stats->cycles, stats->spills, stats->fills, stats->scratch_memory_size, stats->grf_registers, stats->push_constant_ranges, stats->push_constant_registers, stats->max_dispatch_width, stats->max_live_registers, stats->workgroup_memory_size, stats->non_ssa_regs_after_nir, stats->scheduler_mode);
}

#define vk_add_genisa_stats(out, stats) do { \
   vk_add_exec_statistic_u64(out, "Instructions", "Number of GEN instructions in the final generated shader executable.", (stats)->instrs); \
   vk_add_exec_statistic_u64(out, "Code size", "Code size in bytes", (stats)->code_size); \
   vk_add_exec_statistic_u64(out, "SENDs", "Number of instructions in the final generated shader executable which access external units such as the constant cache or the sampler.", (stats)->sends); \
   vk_add_exec_statistic_u64(out, "Loops", "Number of loops (not unrolled) in the final generated shader executable.", (stats)->loops); \
   vk_add_exec_statistic_u64(out, "Cycles", "Estimate of the number of EU cycles required to execute the final generated executable. This is an estimate only and may vary greatly from actual run-time performance.", (stats)->cycles); \
   vk_add_exec_statistic_u64(out, "Spills", "Number of scratch spill operations. This gives a rough estimate of the cost incurred due to spilling temporary values to memory. If this is non-zero, you may want to adjust your shader to reduce register pressure.", (stats)->spills); \
   vk_add_exec_statistic_u64(out, "Fills", "Number of scratch fill operations. This gives a rough estimate of the cost incurred due to spilling temporary values to memory. If this is non-zero, you may want to adjust your shader to reduce register pressure.", (stats)->fills); \
   vk_add_exec_statistic_u64(out, "Scratch Memory Size", "Number of bytes of scratch memory required by the generated shader executable. If this is non-zero, you may want to adjust your shader to reduce register pressure.", (stats)->scratch_memory_size); \
   vk_add_exec_statistic_u64(out, "GRF registers", "Number of GRF registers required by the shader.", (stats)->grf_registers); \
   vk_add_exec_statistic_u64(out, "Push constant ranges", "Number of push constants ranges provided to the shader.", (stats)->push_constant_ranges); \
   vk_add_exec_statistic_u64(out, "Push constant registers", "Number of GRF registers in the payload registers dedicated to constants.", (stats)->push_constant_registers); \
   vk_add_exec_statistic_u64(out, "Max dispatch width", "Largest SIMD dispatch width.", (stats)->max_dispatch_width); \
   vk_add_exec_statistic_u64(out, "Max live registers", "Maximum number of registers used across the entire shader.", (stats)->max_live_registers); \
   vk_add_exec_statistic_u64(out, "Workgroup Memory Size", "Number of bytes of workgroup shared memory used by this shader including any padding.", (stats)->workgroup_memory_size); \
   vk_add_exec_statistic_u64(out, "Non SSA regs after NIR", "Non SSA regs after NIR translation to BRW.", (stats)->non_ssa_regs_after_nir); \
   vk_add_exec_statistic_u64(out, "Source hash", "Hash generated from shader source or vkd3d-proton provided.", (stats)->source_hash); \
   vk_add_exec_statistic_str(out, "Scheduler mode", "Scheduling mode selected for this binary.", (stats)->scheduler_mode); \
} while(0)

static inline void
genisa_stats_serialize(uint8_t *dest, const struct genisa_stats *stats)
{
    memset(dest, 0, sizeof(*stats)); /* zero initialize any padding */
    memcpy(dest + offsetof(struct genisa_stats, dispatch_width), &stats->dispatch_width, sizeof(uint32_t));
    memcpy(dest + offsetof(struct genisa_stats, max_polygons), &stats->max_polygons, sizeof(uint32_t));
    memcpy(dest + offsetof(struct genisa_stats, instrs), &stats->instrs, sizeof(uint32_t));
    memcpy(dest + offsetof(struct genisa_stats, code_size), &stats->code_size, sizeof(uint32_t));
    memcpy(dest + offsetof(struct genisa_stats, sends), &stats->sends, sizeof(uint32_t));
    memcpy(dest + offsetof(struct genisa_stats, loops), &stats->loops, sizeof(uint32_t));
    memcpy(dest + offsetof(struct genisa_stats, cycles), &stats->cycles, sizeof(uint32_t));
    memcpy(dest + offsetof(struct genisa_stats, spills), &stats->spills, sizeof(uint32_t));
    memcpy(dest + offsetof(struct genisa_stats, fills), &stats->fills, sizeof(uint32_t));
    memcpy(dest + offsetof(struct genisa_stats, scratch_memory_size), &stats->scratch_memory_size, sizeof(uint32_t));
    memcpy(dest + offsetof(struct genisa_stats, grf_registers), &stats->grf_registers, sizeof(uint32_t));
    memcpy(dest + offsetof(struct genisa_stats, push_constant_ranges), &stats->push_constant_ranges, sizeof(uint32_t));
    memcpy(dest + offsetof(struct genisa_stats, push_constant_registers), &stats->push_constant_registers, sizeof(uint32_t));
    memcpy(dest + offsetof(struct genisa_stats, max_dispatch_width), &stats->max_dispatch_width, sizeof(uint32_t));
    memcpy(dest + offsetof(struct genisa_stats, max_live_registers), &stats->max_live_registers, sizeof(uint32_t));
    memcpy(dest + offsetof(struct genisa_stats, workgroup_memory_size), &stats->workgroup_memory_size, sizeof(uint32_t));
    memcpy(dest + offsetof(struct genisa_stats, non_ssa_regs_after_nir), &stats->non_ssa_regs_after_nir, sizeof(uint32_t));
    memcpy(dest + offsetof(struct genisa_stats, source_hash), &stats->source_hash, sizeof(uint64_t));
    memcpy(dest + offsetof(struct genisa_stats, scheduler_mode), &stats->scheduler_mode, sizeof(const char *));
}

struct rogue_stats {
   uint32_t code_size;
   uint32_t scratch_size;
   uint32_t spill_count;
   uint32_t temp_count;
   uint32_t vtxin_count;
   uint32_t loop_count;
   uint32_t inst_group_count;
   uint32_t main_inst_group_count;
   uint32_t bitwise_inst_group_count;
   uint32_t control_inst_group_count;
};

static inline int
rogue_stats_fprintf(FILE *fp, const char *prefix, const struct rogue_stats *stats)
{
   return fprintf(fp, "%s shader: %" PRIu32 " code size, %" PRIu32 " scratch size, %" PRIu32 " spill count, %" PRIu32 " temp count, %" PRIu32 " vtxin count, %" PRIu32 " loop count, %" PRIu32 " inst group count, %" PRIu32 " main inst group count, %" PRIu32 " bitwise inst group count, %" PRIu32 " control inst group count\n", prefix, stats->code_size, stats->scratch_size, stats->spill_count, stats->temp_count, stats->vtxin_count, stats->loop_count, stats->inst_group_count, stats->main_inst_group_count, stats->bitwise_inst_group_count, stats->control_inst_group_count);
}

static inline void
rogue_stats_util_debug(struct util_debug_callback *debug, const char *prefix, const struct rogue_stats *stats)
{
   util_debug_message(debug, SHADER_INFO, "%s shader: %" PRIu32 " code size, %" PRIu32 " scratch size, %" PRIu32 " spill count, %" PRIu32 " temp count, %" PRIu32 " vtxin count, %" PRIu32 " loop count, %" PRIu32 " inst group count, %" PRIu32 " main inst group count, %" PRIu32 " bitwise inst group count, %" PRIu32 " control inst group count", prefix, stats->code_size, stats->scratch_size, stats->spill_count, stats->temp_count, stats->vtxin_count, stats->loop_count, stats->inst_group_count, stats->main_inst_group_count, stats->bitwise_inst_group_count, stats->control_inst_group_count);
}

#define vk_add_rogue_stats(out, stats) do { \
   vk_add_exec_statistic_u64(out, "Code size", "Binary size in bytes", (stats)->code_size); \
   vk_add_exec_statistic_u64(out, "Scratch size", "Scratch size per instance in bytes", (stats)->scratch_size); \
   vk_add_exec_statistic_u64(out, "Spill count", "Number of spilled registers per instance", (stats)->spill_count); \
   vk_add_exec_statistic_u64(out, "Temp count", "Number of allocated temp registers", (stats)->temp_count); \
   vk_add_exec_statistic_u64(out, "Vtxin count", "Number of used vertex input registers", (stats)->vtxin_count); \
   vk_add_exec_statistic_u64(out, "Loop count", "Number of not unrolled loops in the shader", (stats)->loop_count); \
   vk_add_exec_statistic_u64(out, "Inst group count", "Total number of instruction groups", (stats)->inst_group_count); \
   vk_add_exec_statistic_u64(out, "Main inst group count", "Number of main instruction groups", (stats)->main_inst_group_count); \
   vk_add_exec_statistic_u64(out, "Bitwise inst group count", "Number of bitwise instruction groups", (stats)->bitwise_inst_group_count); \
   vk_add_exec_statistic_u64(out, "Control inst group count", "Number of control instruction groups", (stats)->control_inst_group_count); \
} while(0)

static inline void
rogue_stats_serialize(uint8_t *dest, const struct rogue_stats *stats)
{
    memset(dest, 0, sizeof(*stats)); /* zero initialize any padding */
    memcpy(dest + offsetof(struct rogue_stats, code_size), &stats->code_size, sizeof(uint32_t));
    memcpy(dest + offsetof(struct rogue_stats, scratch_size), &stats->scratch_size, sizeof(uint32_t));
    memcpy(dest + offsetof(struct rogue_stats, spill_count), &stats->spill_count, sizeof(uint32_t));
    memcpy(dest + offsetof(struct rogue_stats, temp_count), &stats->temp_count, sizeof(uint32_t));
    memcpy(dest + offsetof(struct rogue_stats, vtxin_count), &stats->vtxin_count, sizeof(uint32_t));
    memcpy(dest + offsetof(struct rogue_stats, loop_count), &stats->loop_count, sizeof(uint32_t));
    memcpy(dest + offsetof(struct rogue_stats, inst_group_count), &stats->inst_group_count, sizeof(uint32_t));
    memcpy(dest + offsetof(struct rogue_stats, main_inst_group_count), &stats->main_inst_group_count, sizeof(uint32_t));
    memcpy(dest + offsetof(struct rogue_stats, bitwise_inst_group_count), &stats->bitwise_inst_group_count, sizeof(uint32_t));
    memcpy(dest + offsetof(struct rogue_stats, control_inst_group_count), &stats->control_inst_group_count, sizeof(uint32_t));
}


enum pan_stat_isa {
   PAN_STAT_MIDGARD,
   PAN_STAT_BIFROST,
   PAN_STAT_VALHALL,
};

struct pan_stats {
   enum pan_stat_isa isa;
   union {
      struct midgard_stats midgard;
      struct bifrost_stats bifrost;
      struct valhall_stats valhall;
   };
};

#define vk_add_pan_stats(out, stats) do { \
    if ((stats)->isa == PAN_STAT_MIDGARD) \
       vk_add_midgard_stats(out, &(stats)->midgard); \
    if ((stats)->isa == PAN_STAT_BIFROST) \
       vk_add_bifrost_stats(out, &(stats)->bifrost); \
    if ((stats)->isa == PAN_STAT_VALHALL) \
       vk_add_valhall_stats(out, &(stats)->valhall); \
} while(0)

static inline void
pan_stats_fprintf(FILE *fp, const char *prefix, const struct pan_stats *stats)
{
    if (stats->isa == PAN_STAT_MIDGARD)
       midgard_stats_fprintf(fp, prefix, &stats->midgard);
    if (stats->isa == PAN_STAT_BIFROST)
       bifrost_stats_fprintf(fp, prefix, &stats->bifrost);
    if (stats->isa == PAN_STAT_VALHALL)
       valhall_stats_fprintf(fp, prefix, &stats->valhall);
}

static inline void
pan_stats_util_debug(struct util_debug_callback *debug, const char *prefix, const struct pan_stats *stats)
{
    if (stats->isa == PAN_STAT_MIDGARD)
       midgard_stats_util_debug(debug, prefix, &stats->midgard);
    if (stats->isa == PAN_STAT_BIFROST)
       bifrost_stats_util_debug(debug, prefix, &stats->bifrost);
    if (stats->isa == PAN_STAT_VALHALL)
       valhall_stats_util_debug(debug, prefix, &stats->valhall);
}

static inline void
pan_stats_serialize(uint8_t *dest, const struct pan_stats *stats)
{
    if (stats->isa == PAN_STAT_MIDGARD)
       midgard_stats_serialize(dest, &stats->midgard);
    if (stats->isa == PAN_STAT_BIFROST)
       bifrost_stats_serialize(dest, &stats->bifrost);
    if (stats->isa == PAN_STAT_VALHALL)
       valhall_stats_serialize(dest, &stats->valhall);
}

enum intel_stat_isa {
   INTEL_STAT_GENISA,
};

struct intel_stats {
   enum intel_stat_isa isa;
   union {
      struct genisa_stats genisa;
   };
};

#define vk_add_intel_stats(out, stats) do { \
    if ((stats)->isa == INTEL_STAT_GENISA) \
       vk_add_genisa_stats(out, &(stats)->genisa); \
} while(0)

static inline void
intel_stats_fprintf(FILE *fp, const char *prefix, const struct intel_stats *stats)
{
    if (stats->isa == INTEL_STAT_GENISA)
       genisa_stats_fprintf(fp, prefix, &stats->genisa);
}

static inline void
intel_stats_util_debug(struct util_debug_callback *debug, const char *prefix, const struct intel_stats *stats)
{
    if (stats->isa == INTEL_STAT_GENISA)
       genisa_stats_util_debug(debug, prefix, &stats->genisa);
}

static inline void
intel_stats_serialize(uint8_t *dest, const struct intel_stats *stats)
{
    if (stats->isa == INTEL_STAT_GENISA)
       genisa_stats_serialize(dest, &stats->genisa);
}

enum pvr_stat_isa {
   PVR_STAT_ROGUE,
};

struct pvr_stats {
   enum pvr_stat_isa isa;
   union {
      struct rogue_stats rogue;
   };
};

#define vk_add_pvr_stats(out, stats) do { \
    if ((stats)->isa == PVR_STAT_ROGUE) \
       vk_add_rogue_stats(out, &(stats)->rogue); \
} while(0)

static inline void
pvr_stats_fprintf(FILE *fp, const char *prefix, const struct pvr_stats *stats)
{
    if (stats->isa == PVR_STAT_ROGUE)
       rogue_stats_fprintf(fp, prefix, &stats->rogue);
}

static inline void
pvr_stats_util_debug(struct util_debug_callback *debug, const char *prefix, const struct pvr_stats *stats)
{
    if (stats->isa == PVR_STAT_ROGUE)
       rogue_stats_util_debug(debug, prefix, &stats->rogue);
}

static inline void
pvr_stats_serialize(uint8_t *dest, const struct pvr_stats *stats)
{
    if (stats->isa == PVR_STAT_ROGUE)
       rogue_stats_serialize(dest, &stats->rogue);
}


#endif

