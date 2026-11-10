#pragma once

// PS4 stub for miniupnpc.h (UPnP not available on PS4)

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UPNPDev {
    char* descURL;
    char* st;
    unsigned int scope_id;
    struct UPNPDev* pNext;
} UPNPDev;

typedef struct UPNPUrls {
    char* controlURL;
    char* ipconURL;
    char* eventSubURL;
    char* controlURL_CIF;
    char* controlURL_6FC;
} UPNPUrls;

typedef struct IGDdatas {
    char lanaddr[64];
    char presentationurl[128];
    char servicetype[128];
    char controlurl[128];
    char devicetype[128];
    char friendlyname[128];
    char urlbase[128];
    struct IGDdatas_service {
        char controlurl[128];
        char eventsuburl[128];
        char servicetype[128];
    } first;
} IGDdatas;

#define UPNPDISCOVER_SUCCESS 0
#define UPNPCOMMAND_SUCCESS 0

static inline UPNPDev* upnpDiscover(int delay, const char* multicastif, const char* minissdpdsock, int localport, int ipv6, unsigned char ttl, int* error) { if (error) *error = -1; return nullptr; }
static inline void freeUPNPDevlist(UPNPDev* devlist) {}
static inline int UPNP_GetValidIGD(UPNPDev* devlist, UPNPUrls* urls, IGDdatas* data, char* lanaddr, int lanaddrlen) { return 0; }
static inline void FreeUPNPUrls(UPNPUrls* urls) {}
static inline void GetUPNPUrls(UPNPUrls* urls, IGDdatas* data, const char* descURL, int ipv6) {}

#ifdef __cplusplus
}
#endif
