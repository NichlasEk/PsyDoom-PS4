#ifndef _COMPAT_MACHINE_ENDIAN_H
#define _COMPAT_MACHINE_ENDIAN_H

// PS4 OpenOrbis SDK provides <endian.h> but not <machine/endian.h>.
// LLVM's bit.h includes <machine/endian.h> on BSD-like systems.
// Forward to the SDK's <endian.h>.
#include <endian.h>

#endif
