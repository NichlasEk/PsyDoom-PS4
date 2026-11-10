#pragma once
// Stub libavutil/imgutils.h for PS4 (ffmpeg not available)
#include <stdint.h>

static inline int av_image_fill_linesizes(int* linesizes, int pix_fmt, int width) {
    if (linesizes) *linesizes = width;
    return 0;
}
static inline int av_image_fill_pointers(uint8_t* data[4], int pix_fmt, int height, uint8_t* src, const int linesizes[4]) {
    if (data) data[0] = src;
    return 0;
}
static inline int av_image_get_buffer_size(int pix_fmt, int width, int height, int align) {
    return width * height * 4;
}
