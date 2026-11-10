#pragma once
// PS4 stub for upnpcommands.h (miniupnpc not available on PS4)

#include "miniupnpc.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _UPNPCOMMANDS_H_PS4_STUB
#define _UPNPCOMMANDS_H_PS4_STUB

static inline int UPNP_AddPortMapping(const char* controlURL, const char* servicetype, const char* extPort, const char* inPort, const char* inClient, const char* desc, const char* proto, const char* remoteHost, const char* leaseDuration) { return 0; }
static inline int UPNP_DeletePortMapping(const char* controlURL, const char* servicetype, const char* extPort, const char* proto, const char* remoteHost) { return 0; }
static inline int UPNP_GetSpecificPortMappingEntry(const char* controlURL, const char* servicetype, const char* extPort, const char* proto, const char* remoteHost, char* intClient, char* intPort, char* desc, char* enabled, char* leaseDuration) { return 0; }
static inline int UPNP_GetExternalIPAddress(const char* controlURL, const char* servicetype, char* extIp) { return 0; }

#endif

#ifdef __cplusplus
}
#endif
