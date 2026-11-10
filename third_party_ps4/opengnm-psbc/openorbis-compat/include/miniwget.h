#pragma once
// PS4 stub for miniwget.h (miniupnpc not available on PS4)
#include "miniupnpc.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void* miniwget_getaddr(const char* url, int* size, int* status, int ipv6) { if (size) *size = 0; if (status) *status = 0; return nullptr; }
static inline void* miniwget(const char* url, int* size, int ipv6, int* status) { if (size) *size = 0; if (status) *status = 0; return nullptr; }
static inline void parserootdesc(const char* buffer, int bufsize, IGDdatas* data) {}

#ifdef __cplusplus
}
#endif
