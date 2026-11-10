#pragma once
#include <vulkan/vulkan.h>

// One fixed fullscreen target, matching the tested OpenGNM VideoOut path.
constexpr uint32_t PS4_RENDER_WIDTH = 1280;
constexpr uint32_t PS4_RENDER_HEIGHT = 720;
VkSurfaceKHR PS4CreateVideoOutSurface(VkInstance instance);
extern "C" PFN_vkVoidFunction PS4VulkanGetInstanceProcAddr(VkInstance instance, const char* name);
