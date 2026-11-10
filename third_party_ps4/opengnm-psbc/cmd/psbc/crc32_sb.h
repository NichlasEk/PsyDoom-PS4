#ifndef _CRC32_SB_H_
#define _CRC32_SB_H_

#include <stddef.h>
#include <stdint.h>

static inline uint32_t crc32_sb_begin(void) {
	return 0xffffffff;
}

uint32_t crc32_sb(const void* data, size_t size, uint32_t startcrc);

static inline uint32_t crc32_sb_end(uint32_t crc) {
	return ~crc;
}

#endif	// _CRC32_SB_H_
