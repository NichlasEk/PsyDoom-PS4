#include "videoout_surface.h"
#include <cstdio>
#include <initializer_list>
extern "C" int psydoomLogVulkanCapabilities() {
    VkApplicationInfo app={}; app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO; app.apiVersion=VK_API_VERSION_1_0;
    VkInstanceCreateInfo info={}; info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; info.pApplicationInfo=&app;
    VkInstance instance=VK_NULL_HANDLE;
    VkResult result=vkCreateInstance(&info,nullptr,&instance);
    std::printf("Probe vkCreateInstance=%d\n",result);
    if(result!=VK_SUCCESS) return 1;
    VkPhysicalDevice physical=VK_NULL_HANDLE; uint32_t count=1;
    result=vkEnumeratePhysicalDevices(instance,&count,&physical);
    std::printf("Probe enumerate=%d count=%u\n",result,count);
    if(result==VK_SUCCESS && count && physical) {
        VkPhysicalDeviceProperties props={}; vkGetPhysicalDeviceProperties(physical,&props);
        std::printf("GPU=%s api=%u maxTexture=%u\n",props.deviceName,props.apiVersion,props.limits.maxImageDimension2D);
        for(VkFormat format : {VK_FORMAT_R16_UINT,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_A1R5G5B5_UNORM_PACK16}) {
            VkFormatProperties formats={}; vkGetPhysicalDeviceFormatProperties(physical,format,&formats);
            std::printf("Format=%d linear=%x optimal=%x buffer=%x\n",format,formats.linearTilingFeatures,formats.optimalTilingFeatures,formats.bufferFeatures);
        }
    }
    vkDestroyInstance(instance,nullptr);
    return result==VK_SUCCESS && count && physical ? 0 : 1;
}
