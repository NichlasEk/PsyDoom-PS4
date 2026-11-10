#ifndef RADV_INSTANCE_H
#define RADV_INSTANCE_H

#include <stdint.h>

enum radv_debug_flags {
   RADV_DEBUG_DUMP_VS = 1ull << 42,
   RADV_DEBUG_DUMP_TCS = 1ull << 43,
   RADV_DEBUG_DUMP_TES = 1ull << 44,
   RADV_DEBUG_DUMP_GS = 1ull << 45,
   RADV_DEBUG_DUMP_PS = 1ull << 46,
   RADV_DEBUG_DUMP_TASK = 1ull << 47,
   RADV_DEBUG_DUMP_MESH = 1ull << 48,
   RADV_DEBUG_DUMP_CS = 1ull << 49,
   RADV_DEBUG_DUMP_NIR = 1ull << 50,
   RADV_DEBUG_DUMP_ASM = 1ull << 51,
   RADV_DEBUG_DUMP_BACKEND_IR = 1ull << 52,
   RADV_DEBUG_DUMP_TRAP_HANDLER = 1ull << 53,
   RADV_DEBUG_DUMP_PROLOGS = 1ull << 54,
   RADV_DEBUG_DUMP_EPILOGS = 1ull << 55,
   RADV_DEBUG_DUMP_SHADERS = RADV_DEBUG_DUMP_VS | RADV_DEBUG_DUMP_TCS | RADV_DEBUG_DUMP_TES | RADV_DEBUG_DUMP_GS |
                             RADV_DEBUG_DUMP_PS | RADV_DEBUG_DUMP_TASK | RADV_DEBUG_DUMP_MESH | RADV_DEBUG_DUMP_CS |
                             RADV_DEBUG_DUMP_NIR | RADV_DEBUG_DUMP_ASM | RADV_DEBUG_DUMP_BACKEND_IR,
};

enum {
   RADV_TRAP_EXCP_MEM_VIOL = 1u << 0,
   RADV_TRAP_EXCP_FLOAT_DIV_BY_ZERO = 1u << 1,
   RADV_TRAP_EXCP_FLOAT_OVERFLOW = 1u << 2,
   RADV_TRAP_EXCP_FLOAT_UNDERFLOW = 1u << 3,
};

struct radv_instance {
   uint64_t debug_flags;
};

static inline const struct radv_instance *
radv_physical_device_instance(const struct radv_physical_device *pdev)
{
   (void)pdev;
   static struct radv_instance default_instance = {0};
   return &default_instance;
}

#endif
