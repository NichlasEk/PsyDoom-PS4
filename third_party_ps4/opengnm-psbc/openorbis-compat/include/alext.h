#pragma once
// PS4 stub for alext.h (OpenAL extensions not available on PS4)

#include "AL/alc.h"
#include "AL/al.h"

// Extension check stubs
static inline ALboolean alcIsExtensionPresent(ALCdevice* device, const ALchar* extname) { return AL_FALSE; }
static inline void* alcGetProcAddress(ALCdevice* device, const ALchar* extname) { return nullptr; }
static inline ALboolean alIsExtensionPresent(const ALchar* extname) { return AL_FALSE; }
static inline void* alGetProcAddress(const ALchar* extname) { return nullptr; }

// Additional format constants
#ifndef AL_FORMAT_QUAD16
#define AL_FORMAT_QUAD16 0x1204
#endif
#ifndef AL_FORMAT_QUAD8
#define AL_FORMAT_QUAD8 0x1203
#endif
#ifndef AL_FORMAT_51CHN16
#define AL_FORMAT_51CHN16 0x1206
#endif
#ifndef AL_FORMAT_51CHN8
#define AL_FORMAT_51CHN8 0x1205
#endif
#ifndef AL_FORMAT_61CHN16
#define AL_FORMAT_61CHN16 0x1208
#endif
#ifndef AL_FORMAT_71CHN16
#define AL_FORMAT_71CHN16 0x120A
#endif

#ifndef AL_TRUE
#define AL_TRUE 1
#endif
#ifndef AL_FALSE
#define AL_FALSE 0
#endif
