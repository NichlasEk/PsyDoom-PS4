#pragma once
// PS4 stub for wolfssl/wolfcrypt/coding.h

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int Base64_Decode(const unsigned char* in, int inLen, unsigned char* out, int* outLen) { *outLen = 0; return 0; }
static inline int Base64_Encode(const unsigned char* in, int inLen, unsigned char* out, int* outLen) { *outLen = 0; return 0; }

#ifdef __cplusplus
}
#endif
