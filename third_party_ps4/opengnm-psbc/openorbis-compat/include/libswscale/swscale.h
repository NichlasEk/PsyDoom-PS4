#pragma once
// Stub libswscale/swscale.h for PS4 (ffmpeg not available)
#include "../libavutil/pixfmt.h"
typedef struct SwsContext SwsContext;

#define SWS_FAST_BILINEAR 1
#define SWS_BILINEAR 2
#define SWS_BICUBIC 4
#define SWS_POINT 0x10

static inline SwsContext* sws_getContext(int srcW, int srcH, int srcFormat,
                                          int dstW, int dstH, int dstFormat,
                                          int flags, void* srcFilter, void* dstFilter, double* param) { return nullptr; }
static inline void sws_freeContext(SwsContext* ctx) {}
static inline SwsContext* sws_getCachedContext(SwsContext* ctx, int srcW, int srcH, int srcFormat,
                                                 int dstW, int dstH, int dstFormat,
                                                 int flags, void* srcFilter, void* dstFilter, double* param) { return nullptr; }
static inline int sws_scale(SwsContext* ctx, const unsigned char* const srcSlice[], const int srcStride[],
                            int srcSliceY, int srcSliceH, unsigned char* const dst[], const int dstStride[]) { return 0; }
