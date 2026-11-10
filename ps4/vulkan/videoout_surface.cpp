// Adapted from ut99-orbis/platform/ps4/videoout_surface.cpp.
// Modified for PsyDoom: direct ICD symbols and transfer-destination swapchain.
// Original license: UT99-LICENSE.txt.
#include "videoout_surface.h"
#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <memory>

extern "C" PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance, const char*);
extern "C" PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice, const char*);

namespace {
struct VideoOutSurface { VkInstance instance; };
std::unordered_map<VkSurfaceKHR, std::unique_ptr<VideoOutSurface>> surfaces;
VideoOutSurface* find(VkSurfaceKHR handle) {
    auto it = surfaces.find(handle);
    return it == surfaces.end() ? nullptr : it->second.get();
}
VKAPI_ATTR void VKAPI_CALL destroySurface(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks*) {
    if (!surface) return;
    auto* target = find(surface);
    if (!target || target->instance != instance) return;
    surfaces.erase(surface);
}
VKAPI_ATTR VkResult VKAPI_CALL surfaceSupport(VkPhysicalDevice physical, uint32_t family, VkSurfaceKHR surface, VkBool32* supported) {
    if (!find(surface) || !physical || !supported) return VK_ERROR_SURFACE_LOST_KHR;
    uint32_t count = 0;
    auto query = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(vkGetInstanceProcAddr(find(surface)->instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    query(physical, &count, nullptr);
    // OpenGNM submits VideoOut flips through the graphics family (index 0).
    *supported = family < count && family == 0;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL surfaceCapabilities(VkPhysicalDevice physical, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* caps) {
    if (!find(surface) || !physical || !caps) return VK_ERROR_SURFACE_LOST_KHR;
    *caps = {};
    caps->minImageCount = 2;
    caps->maxImageCount = 3;
    caps->currentExtent = caps->minImageExtent = caps->maxImageExtent = {PS4_RENDER_WIDTH, PS4_RENDER_HEIGHT};
    caps->maxImageArrayLayers = 1;
    caps->supportedTransforms = caps->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    caps->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    caps->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL surfaceFormats(VkPhysicalDevice physical, VkSurfaceKHR surface, uint32_t* count, VkSurfaceFormatKHR* formats) {
    if (!find(surface) || !physical || !count) return VK_ERROR_SURFACE_LOST_KHR;
    if (!formats) { *count = 1; return VK_SUCCESS; }
    if (!*count) return VK_INCOMPLETE;
    *formats = {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    *count = 1;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL presentModes(VkPhysicalDevice physical, VkSurfaceKHR surface, uint32_t* count, VkPresentModeKHR* modes) {
    if (!find(surface) || !physical || !count) return VK_ERROR_SURFACE_LOST_KHR;
    if (!modes) { *count = 1; return VK_SUCCESS; }
    if (!*count) return VK_INCOMPLETE;
    *modes = VK_PRESENT_MODE_FIFO_KHR;
    *count = 1;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL createSwapchain(VkDevice device, const VkSwapchainCreateInfoKHR* info, const VkAllocationCallbacks* allocator, VkSwapchainKHR* out) {
    if (!info || !out || !device) return VK_ERROR_INITIALIZATION_FAILED;
    if (!find(info->surface)) return VK_ERROR_SURFACE_LOST_KHR;
    if (info->imageExtent.width != PS4_RENDER_WIDTH || info->imageExtent.height != PS4_RENDER_HEIGHT ||
        info->imageFormat != VK_FORMAT_R8G8B8A8_UNORM || info->imageColorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ||
        info->presentMode != VK_PRESENT_MODE_FIFO_KHR || info->minImageCount < 2 || info->minImageCount > 3 ||
        info->imageArrayLayers != 1 || info->imageUsage != (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) || info->oldSwapchain)
        return VK_ERROR_INITIALIZATION_FAILED;
    // The native driver owns the actual VideoOut handle and display buffers,
    // opens on swapchain creation, and closes on vkDestroySwapchainKHR.
    auto create = reinterpret_cast<PFN_vkCreateSwapchainKHR>(vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR"));
    return create(device, info, allocator, out);
}
const char* promotedAlias(const char* name) {
    // These promoted entrypoints have identical core/KHR signatures.
    static const char* aliases[][2] = {
        {"vkGetBufferMemoryRequirements2KHR", "vkGetBufferMemoryRequirements2"},
        {"vkGetImageMemoryRequirements2KHR", "vkGetImageMemoryRequirements2"},
        {"vkBindBufferMemory2KHR", "vkBindBufferMemory2"},
        {"vkBindImageMemory2KHR", "vkBindImageMemory2"},
        {"vkGetPhysicalDeviceMemoryProperties2KHR", "vkGetPhysicalDeviceMemoryProperties2"},
        {"vkGetPhysicalDeviceProperties2KHR", "vkGetPhysicalDeviceProperties2"},
        {"vkGetPhysicalDeviceFeatures2KHR", "vkGetPhysicalDeviceFeatures2"}
    };
    for (const auto& alias : aliases) if (name && std::strcmp(name, alias[0]) == 0) return alias[1];
    return name;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL deviceProc(VkDevice device, const char* name) {
    if (name && std::strcmp(name, "vkCreateSwapchainKHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(createSwapchain);
    return vkGetDeviceProcAddr(device, promotedAlias(name));
}
}
VkSurfaceKHR PS4CreateVideoOutSurface(VkInstance instance) {
    if (!instance) throw std::runtime_error("VideoOut surface requires a Vulkan instance");
    if (!surfaces.empty()) throw std::runtime_error("Only one PS4 fullscreen surface is supported");
    auto target = std::make_unique<VideoOutSurface>();
    target->instance = instance;
    auto handle = reinterpret_cast<VkSurfaceKHR>(target.get());
    surfaces.emplace(handle, std::move(target));
    return handle;
}
extern "C" PFN_vkVoidFunction PS4VulkanGetInstanceProcAddr(VkInstance instance, const char* name) {
    if (!name) return nullptr;
#define WSI_PROC(vkName, function) if (std::strcmp(name, #vkName) == 0) return reinterpret_cast<PFN_vkVoidFunction>(function)
    WSI_PROC(vkDestroySurfaceKHR, destroySurface);
    WSI_PROC(vkGetPhysicalDeviceSurfaceSupportKHR, surfaceSupport);
    WSI_PROC(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, surfaceCapabilities);
    WSI_PROC(vkGetPhysicalDeviceSurfaceFormatsKHR, surfaceFormats);
    WSI_PROC(vkGetPhysicalDeviceSurfacePresentModesKHR, presentModes);
    WSI_PROC(vkCreateSwapchainKHR, createSwapchain);
    WSI_PROC(vkGetDeviceProcAddr, deviceProc);
#undef WSI_PROC
    return vkGetInstanceProcAddr(instance, promotedAlias(name));
}
