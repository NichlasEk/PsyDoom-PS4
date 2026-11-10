#ifndef VK_LOG_H
#define VK_LOG_H

#include <stdarg.h>
#include <stdint.h>
#include "vulkan/vulkan_core.h"

struct vk_instance;
struct vk_physical_device;
struct vk_device;

static inline void vk_logd(struct vk_instance *i, const char *fmt, ...) { (void)i; (void)fmt; }
static inline void vk_logi(struct vk_instance *i, const char *fmt, ...) { (void)i; (void)fmt; }
static inline void vk_logw(struct vk_instance *i, const char *fmt, ...) { (void)i; (void)fmt; }
static inline void vk_loge(struct vk_instance *i, const char *fmt, ...) { (void)i; (void)fmt; }

#endif
