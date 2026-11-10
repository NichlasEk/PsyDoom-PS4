#pragma once
#if defined(__PS4__)
#include "Buffer.h"
#include "BaseTexture.h"
#include "CmdBufferRecorder.h"
#include "DescriptorPool.h"
#include "DescriptorSet.h"
#include "FatalErrors.h"
#include <cstring>
#include "VTypes.h"
static_assert(offsetof(VShaderUniforms_Draw, ndcToPsxScaleX) == 64);
static_assert(offsetof(VShaderUniforms_Draw, psxNdcOffsetX) == 72);
static_assert(offsetof(VShaderUniforms_Draw, invDrawResX) == 80);

// Each recorded uniform update owns immutable storage and descriptors until
// the retirement manager observes completion of the current GPU frame.
inline void bindPs4Uniforms(vgl::CmdBufferRecorder& recorder, vgl::Pipeline& pipeline,
    vgl::LogicalDevice& device, const vgl::DescriptorSetLayout& layout,
    const void* values, size_t size, const vgl::Sampler& sampler,
    const vgl::BaseTexture& texture, const vgl::BaseTexture* second = nullptr) noexcept {
    if (size > 128) FatalErrors::raise("PS4 uniform block exceeds 128 bytes");
    vgl::Buffer buffer;
    if (!buffer.initWithByteCount(device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        vgl::BufferUsageMode::IMMEDIATE, 128))
        FatalErrors::raise("PS4 uniform buffer allocation failed");
    auto* bytes = buffer.lockBytes(0, 128);
    if (!bytes) FatalErrors::raise("PS4 uniform buffer mapping failed");
    std::memset(bytes, 0, 128);
    std::memcpy(bytes, values, size);
    const float inverseSize[] = {1.0f / texture.getWidth(), 1.0f / texture.getHeight()};
    std::memcpy(bytes + 96, inverseSize, sizeof(inverseSize));
    buffer.unlockBytes(128);
    vgl::DescriptorPool pool;
    if (!pool.init(device, {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, second ? 2u : 1u}}, 1))
        FatalErrors::raise("PS4 uniform descriptor pool allocation failed");
    auto* set = pool.allocDescriptorSet(layout);
    if (!set) FatalErrors::raise("PS4 uniform descriptor allocation failed");
    set->bindTextureAndSampler(0, texture, sampler);
    if (second) set->bindTextureAndSampler(1, *second, sampler);
    set->bindBufferBytes(8, buffer, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, 128);
    recorder.bindDescriptorSet(*set, pipeline, 0);
    set->free();
    pool.destroy();
    buffer.destroy();
}
#endif
