#pragma once
// PS4 stub for AL/alc.h (OpenAL not available on PS4)

typedef char ALCchar;
typedef int ALCboolean;
typedef int ALCint;
typedef unsigned int ALCuint;
typedef unsigned int ALCenum;
typedef int ALCsizei;
typedef void ALCvoid;
typedef struct ALCdevice ALCdevice;
typedef struct ALCcontext ALCcontext;

#define ALC_NO_ERROR 0
#define ALC_INVALID_DEVICE 0xA001
#define ALC_INVALID_CONTEXT 0xA002
#define ALC_FALSE 0
#define ALC_TRUE 1
#define ALC_CAPTURE_DEVICE_SPECIFIER 0x310
#define ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER 0x311
#define ALC_CAPTURE_SAMPLES 0x312
#define ALC_DEFAULT_DEVICE_SPECIFIER 0x1004
#define ALC_DEVICE_SPECIFIER 0x1005
#define ALC_EXTENSIONS 0x1006
#define ALC_FREQUENCY 0x1007
#define ALC_REFRESH 0x1008
#define ALC_SYNC 0x1009

static inline ALCdevice* alcOpenDevice(const ALCchar* devname) { return nullptr; }
static inline ALCboolean alcCloseDevice(ALCdevice* device) { return ALC_FALSE; }
static inline ALCcontext* alcCreateContext(ALCdevice* device, const ALCint* attrlist) { return nullptr; }
static inline void alcDestroyContext(ALCcontext* context) {}
static inline ALCboolean alcMakeContextCurrent(ALCcontext* context) { return ALC_FALSE; }
static inline ALCcontext* alcGetCurrentContext(void) { return nullptr; }
static inline ALCenum alcGetError(ALCdevice* device) { return ALC_NO_ERROR; }
static inline const ALCchar* alcGetString(ALCdevice* device, ALCenum param) { return (const ALCchar*)""; }
static inline void alcGetIntegerv(ALCdevice* device, ALCenum param, ALCsizei size, ALCint* values) {}
static inline ALCdevice* alcCaptureOpenDevice(const ALCchar* devname, ALCuint frequency, ALCenum format, ALCsizei buffersize) { return nullptr; }
static inline ALCboolean alcCaptureCloseDevice(ALCdevice* device) { return ALC_FALSE; }
static inline void alcCaptureStart(ALCdevice* device) {}
static inline void alcCaptureStop(ALCdevice* device) {}
static inline void alcCaptureSamples(ALCdevice* device, ALCvoid* buffer, ALCsizei samples) {}
