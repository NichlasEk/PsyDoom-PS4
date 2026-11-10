#include "videoout_surface.h"
#include <cassert>
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance,const char*) {return nullptr;}
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice,const char*) {return nullptr;}
int main() {
 auto instance=reinterpret_cast<VkInstance>(1);
 auto physical=reinterpret_cast<VkPhysicalDevice>(2);
 auto surface=PS4CreateVideoOutSurface(instance);
 auto capsFn=reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(PS4VulkanGetInstanceProcAddr(instance,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
 VkSurfaceCapabilitiesKHR caps={};
 assert(capsFn(physical,surface,&caps)==VK_SUCCESS);
 assert(caps.currentExtent.width==1280 && caps.currentExtent.height==720);
 assert(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT);
 auto formats=reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(PS4VulkanGetInstanceProcAddr(instance,"vkGetPhysicalDeviceSurfaceFormatsKHR"));
 uint32_t count=0; VkSurfaceFormatKHR format={};
 assert(formats(physical,surface,&count,nullptr)==VK_SUCCESS && count==1);
 count=0; assert(formats(physical,surface,&count,&format)==VK_INCOMPLETE);
 count=1; assert(formats(physical,surface,&count,&format)==VK_SUCCESS && format.format==VK_FORMAT_R8G8B8A8_UNORM);
 auto destroy=reinterpret_cast<PFN_vkDestroySurfaceKHR>(PS4VulkanGetInstanceProcAddr(instance,"vkDestroySurfaceKHR"));
 destroy(instance,surface,nullptr);
 assert(capsFn(physical,surface,&caps)==VK_ERROR_SURFACE_LOST_KHR);
 surface=PS4CreateVideoOutSurface(instance); destroy(instance,surface,nullptr);
}
