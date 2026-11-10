/*
 * libpsbc — SPIR-V to PS4/PS5 Shader Binary Compiler Library
 *
 * Extracted from cmd/psbc/main.c to provide a reusable C API for
 * compiling SPIR-V shaders into GCN shader binaries with GnmShaderFileHeader.
 *
 * Used by:
 *   - opengnm-psbc CLI (cmd/psbc/main.c)
 *   - vulkan-ps4 ICD (vkCreateShaderModule)
 */

#ifndef PSBC_COMPILE_H
#define PSBC_COMPILE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* === Types === */

typedef enum {
    PSBC_TARGET_PS4_BASE = 0,   /* GFX7 (Sea Islands) */
    PSBC_TARGET_PS4_NEO  = 1,   /* GFX8 (Polaris) */
    PSBC_TARGET_PS5      = 2,   /* GFX10.3 (RDNA2) */
} PsbcTarget;

typedef enum {
    PSBC_STAGE_NONE       = 0,
    PSBC_STAGE_VERTEX     = 1,
    PSBC_STAGE_TESS_CTRL  = 2,
    PSBC_STAGE_TESS_EVAL  = 3,
    PSBC_STAGE_GEOMETRY   = 4,
    PSBC_STAGE_FRAGMENT   = 5,
    PSBC_STAGE_COMPUTE    = 6,
    PSBC_STAGE_TASK       = 7,
    PSBC_STAGE_EXPORT     = 8,  /* VS as ES (Export Shader) */
    PSBC_STAGE_LOCAL      = 9,  /* VS as LS (Local Shader) */
} PsbcStage;

typedef enum {
    PSBC_RESULT_OK = 0,
    PSBC_RESULT_INVALID_SPIRV,
    PSBC_RESULT_UNSUPPORTED_STAGE,
    PSBC_RESULT_COMPILE_NIR,
    PSBC_RESULT_COMPILE_ACO,
    PSBC_RESULT_OUT_OF_MEMORY,
    PSBC_RESULT_INTERNAL_ERROR,
    PSBC_RESULT_UNSUPPORTED_RESOURCE,
} PsbcResult;

typedef struct {
    void*   data;         /* Compiled shader binary (PSSL header + GNM header + code + trailer) */
    size_t  size;         /* Total size of data in bytes */
} PsbcShaderOutput;

typedef struct {
    PsbcTarget  target;
    PsbcStage   stage;
    const char* entrypoint;  /* Default: "main" */
    bool        optimise;    /* Default: true */
    /* Optional bounded Vulkan set-0 layout, filtered for this shader stage.
     * Zero-init callers retain the standalone ABI. No descriptor arrays. */
    bool validate_resource_layout;
    uint32_t uniform_binding_mask;
    uint32_t texture_binding_mask;
} PsbcCompileOptions;

/* Standalone set-0 descriptor table (not a Vulkan descriptor layout).
 * Bindings 0..15: GnmBuffer at binding*16; GnmTexture at
 * 256+binding*64, with its combined GnmSampler at +32. Texture slots
 * are a separate bank so existing buffer-only tables remain compatible.
 * Runtime must allocate/populate the bank used by each compiled shader.
 */
#define PSBC_TEXTURE_TABLE_OFFSET 256
#define PSBC_TEXTURE_BINDING_STRIDE 64
#define PSBC_COMBINED_SAMPLER_OFFSET 32

/* === API === */

/*
 * Initialize the psbc library. Must be called once before any compilation.
 * Refcounted — call psbc_shutdown() once per psbc_init() when done.
 * Performs Mesa one-time init (glsl_type_singleton, etc.).
 */
void psbc_init(void);

/*
 * Shutdown the psbc library. Decrements refcount, cleans up when it reaches 0.
 */
void psbc_shutdown(void);

/*
 * Compile a SPIR-V shader into a PS4/PS5 shader binary.
 *
 *   spirv       — pointer to SPIR-V bytecode
 *   spirv_size  — size of SPIR-V bytecode in bytes
 *   opts        — compilation options (target, stage, entrypoint, etc.)
 *   out         — receives the compiled binary; caller must free out->data
 *
 * The current standalone binary builder emits vertex and fragment GNM
 * shader headers. Other stages may compile through NIR/ACO but return
 * PSBC_RESULT_UNSUPPORTED_STAGE while their GNM headers are unavailable.
 *
 * Returns PSBC_RESULT_OK on success, error code otherwise.
 * On error, out->data is NULL and out->size is 0.
 */
PsbcResult psbc_compile_shader(
    const uint32_t*       spirv,
    size_t                spirv_size,
    const PsbcCompileOptions* opts,
    PsbcShaderOutput*     out
);

/*
 * Free a PsbcShaderOutput previously filled by psbc_compile_shader().
 */
void psbc_free_output(PsbcShaderOutput* out);

/*
 * Convert a PsbcResult to a human-readable string.
 */
const char* psbc_result_string(PsbcResult result);

/*
 * Convert a stage name string ("vertex", "fragment", etc.) to PsbcStage.
 * Returns PSBC_STAGE_NONE if the name is not recognized.
 */
PsbcStage psbc_stage_from_name(const char* name);

#ifdef __cplusplus
}
#endif

#endif /* PSBC_COMPILE_H */
