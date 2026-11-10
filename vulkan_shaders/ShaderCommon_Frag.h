#include "ShaderCommon.h"
#ifdef PSYDOOM_PS4_UBO
// PS4 compiler supports float sampling. R16_UNORM preserves the original
// 16-bit VRAM words; nearest sampling and rounding recover every bit.
#define usampler2D sampler2D
layout(std140, set = 0, binding = 8) uniform VramDimensions {
    layout(offset = 96) vec2 inverseSize;
} vramDimensions;
uint readVram(usampler2D vram, ivec2 uv) {
    return uint(round(textureLod(vram, (vec2(uv) + 0.5) * vramDimensions.inverseSize, 0.0).r * 65535.0));
}
#else
uint readVram(usampler2D vram, ivec2 uv) { return texelFetch(vram, uv, 0).r; }
#endif

//----------------------------------------------------------------------------------------------------------------------
// Directly read a 16-bit texel from PSX VRAM with no filtering.
// Applies semi-transparency modulations to the pixel if the PSX 'semi-transparent' flag is set.
// Can optionally discard the fragment if the texel is '0' - the same behavior as the original PSX GPU.
//----------------------------------------------------------------------------------------------------------------------
vec4 sampleVramTexel(usampler2D vram, ivec2 uv, vec4 stmul, const bool bAllowDiscard) {
    // If the texel bits are all zero then the pixel is to be discarded
    uint texelBits = readVram(vram, uv);

    if (bAllowDiscard) {
        if (texelBits == 0)
            discard;
    }

    // Unpack the color
    vec4 color = vec4(
        (texelBits >> 0 ) & 0x1F,
        (texelBits >> 5 ) & 0x1F,
        (texelBits >> 10) & 0x1F,
        31.0
    ) / 31.0;

    // Multiply by the semi-transparency multiply if the pixel is semi-transparent
#ifdef PSYDOOM_PS4_UBO
    // Keep this bit selection arithmetic. ACO lowers a conditional bit test
    // to V_CMP_CLASS_F32 with a dynamic class mask, unsupported by shadPS4.
    float semiTransparent = float((texelBits >> 15) & 1u);
    color *= mix(vec4(1.0), stmul, semiTransparent);
#else
    if ((texelBits & 0x8000) != 0) {
        color *= stmul;
    }
#endif

    return color;
}

//----------------------------------------------------------------------------------------------------------------------
// Sample a texel from PSX VRAM at 16 bits per pixel with no filtering.
// Optionally, the fragment can be discarded if the texel is '0' (masking enabled).
//----------------------------------------------------------------------------------------------------------------------
vec4 tex16bpp(
    usampler2D vram,
    ivec2 uv,
    ivec2 texWinPos,
    ivec2 texWinSize,
    vec4 stmul,
    const bool bWrap,
    const bool bAllowDiscard
) {
    // Wrap or clamp the UV coordinate, then transform by the texture window position
    if (bWrap) {
        uv = ivec2(uvec2(uv) % texWinSize);     // N.B use unsigned modulus since signed produces GPU dependent behavior
    } else {
        uv = clamp(uv, ivec2(0, 0), texWinSize - 1);
    }

    uv += texWinPos;

    // Sample the texel directly - no CLUT for 16bpp mode
    return sampleVramTexel(vram, uv, stmul, bAllowDiscard);
}

//----------------------------------------------------------------------------------------------------------------------
// Sample a texel from PSX VRAM at 8 bits per pixel with no filtering.
// Optionally, the fragment can be discarded if the texel is '0' (masking enabled).
//----------------------------------------------------------------------------------------------------------------------
vec4 tex8bpp(
    usampler2D vram,
    ivec2 uv,
    ivec2 texWinPos,
    ivec2 texWinSize,
    ivec2 clutPos,
    vec4 stmul,
    const bool bWrap,
    const bool bAllowDiscard
) {
    // Wrap or clamp the UV coordinate, then transform by the texture window position
    if (bWrap) {
        uv = ivec2(uvec2(uv) % texWinSize);     // N.B use unsigned modulus since signed produces GPU dependent behavior
    } else {
        uv = clamp(uv, ivec2(0, 0), texWinSize - 1);
    }

    uv += texWinPos;

    // Figure out which 8-bits of the 16-bit pixel we will use for the CLUT index and get the CLUT index.
    // Note: must adjust the uv 'x' coordinate for the 8bpp pixel rate since VRAM is 16bpp.
    uint vramTexel = readVram(vram, ivec2(uv.x / 2, uv.y));
    uint byteIdx = uv.x & 1;
    uint clutIdx = vramTexel >> (byteIdx * 8);
    clutIdx &= 0xFF;

    // Sample the texel from the CLUT and return it
    return sampleVramTexel(vram, ivec2(clutPos.x + clutIdx, clutPos.y), stmul, bAllowDiscard);
}

//----------------------------------------------------------------------------------------------------------------------
// Sample a texel from PSX VRAM at 4 bits per pixel with nearest neighbor filtering.
// Optionally, the fragment can be discarded if the texel is '0' (masking enabled).
//----------------------------------------------------------------------------------------------------------------------
vec4 tex4bpp(
    usampler2D vram,
    ivec2 uv,
    ivec2 texWinPos,
    ivec2 texWinSize,
    ivec2 clutPos,
    vec4 stmul,
    const bool bWrap,
    const bool bAllowDiscard
) {
    // Wrap or clamp the UV coordinate, then transform by the texture window position
    if (bWrap) {
        uv = ivec2(uvec2(uv) % texWinSize);     // N.B use unsigned modulus since signed produces GPU dependent behavior
    } else {
        uv = clamp(uv, ivec2(0, 0), texWinSize - 1);
    }

    uv += texWinPos;

    // Figure out which 4-bits of the 16-bit pixel we will use for the CLUT index and get the CLUT index.
    // Note: must adjust the uv 'x' coordinate for the 4bpp pixel rate since VRAM is 16bpp.
    uint vramTexel = readVram(vram, ivec2(uv.x / 4, uv.y));
    uint nibbleIdx = uv.x & 3;
    uint clutIdx = vramTexel >> (nibbleIdx * 4);
    clutIdx &= 0xF;

    // Sample the texel from the CLUT and return it
    return sampleVramTexel(vram, ivec2(clutPos.x + clutIdx, clutPos.y), stmul, bAllowDiscard);
}

//----------------------------------------------------------------------------------------------------------------------
// Quantizes/truncates the given color to R5G5B5 in a way that mimics how the PS1 truncates color.
// Used to help reproduce the shading of the PlayStation.
//----------------------------------------------------------------------------------------------------------------------
vec4 psxR5G5B5BitCrush(vec4 color) {
    // Note: I added a slight amount here to prevent weird rounding/precision issues.
    // This small bias prevents artifacts where pixels rapidly cycle between one 5-bit color component and the next.
    return trunc(color * 31 + 0.0001) / 31;
}
