#pragma once

// Stub libavutil/pixfmt.h for PS4 (ffmpeg not available).
// Provides only the AVPixelFormat enum and the few formats rpcs3 uses.

enum AVPixelFormat {
    AV_PIX_FMT_NONE = -1,
    AV_PIX_FMT_YUV420P = 0,
    AV_PIX_FMT_YUYV422 = 1,
    AV_PIX_FMT_YUVA420P = 2,
    AV_PIX_FMT_YUVJ420P = 12,
    AV_PIX_FMT_RGB24 = 3,
    AV_PIX_FMT_BGR24 = 4,
    AV_PIX_FMT_UYVY422 = 5,
    AV_PIX_FMT_ARGB = 25,
    AV_PIX_FMT_RGBA = 26,
    AV_PIX_FMT_ABGR = 27,
    AV_PIX_FMT_BGRA = 28,
    AV_PIX_FMT_RGB565BE = 44,
    AV_PIX_FMT_RGB565LE = 45,
    AV_PIX_FMT_BGR565BE = 46,
    AV_PIX_FMT_BGR565LE = 47,
    AV_PIX_FMT_RGB555BE = 48,
    AV_PIX_FMT_RGB555LE = 49,
};

typedef enum AVPixelFormat AVPixelFormat;
