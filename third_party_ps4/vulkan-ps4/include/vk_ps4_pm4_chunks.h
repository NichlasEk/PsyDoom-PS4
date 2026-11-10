/* Bounded packet-aligned GNM submissions; preserve embedded GPU addresses. */
#ifndef VK_PS4_PM4_CHUNKS_H
#define VK_PS4_PM4_CHUNKS_H
#include <stdint.h>
#define VK_PS4_PM4_RECORDING_BYTES (16u * 1024u * 1024u)
#define VK_PS4_PM4_SUBMIT_DWORDS 0xfffffu
#define VK_PS4_PM4_MAX_CHUNKS 8u
static inline int vk_ps4_pm4_chunks(const uint32_t *stream, uint32_t words,
    uint32_t limit, uint32_t *offsets, uint32_t *sizes, uint32_t *count) {
    *count = 0;
    if (!stream || !limit || words > VK_PS4_PM4_RECORDING_BYTES / 4) return 0;
    uint32_t pos=0, start=0, n=0;
    while (pos < words) {
        uint32_t header=stream[pos], type=header >> 30;
        if (type == 1) return 0;
        uint32_t length=type == 2 ? 1 : ((header >> 16) & 0x3fff) + 2;
        if (length > words-pos || length > limit) return 0;
        if (pos-start > limit-length) {
            if (n == VK_PS4_PM4_MAX_CHUNKS) return 0;
            offsets[n]=start; sizes[n++]=pos-start; start=pos;
        }
        pos+=length;
    }
    if (pos != start) {
        if (n == VK_PS4_PM4_MAX_CHUNKS) return 0;
        offsets[n]=start; sizes[n++]=pos-start;
    }
    *count=n;
    return 1;
}
#endif
