#ifndef RADV_DEVICE_H
#define RADV_DEVICE_H

#include "radv_physical_device.h"
#include "radv_instance.h"

struct radv_compiler_info;

struct radv_shader_abort_data {
   uint32_t buffer_size;
   uint64_t buffer_addr;
};

struct radv_device {
   struct radv_physical_device *physical_device;
   struct radv_instance *instance;
   struct radv_shader_abort_data shader_abort;
   const struct radv_compiler_info *compiler_info;
};

static inline const struct radv_physical_device *
radv_device_physical(const struct radv_device *device)
{
   return device->physical_device;
}

static inline const struct radv_instance *
radv_device_instance(const struct radv_device *device)
{
   return device->instance;
}

#endif
