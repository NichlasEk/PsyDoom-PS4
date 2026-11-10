#pragma once
// PS4 stub for AL/al.h (OpenAL not available on PS4)

typedef char ALchar;
typedef int ALboolean;
typedef int ALint;
typedef unsigned int ALuint;
typedef unsigned int ALenum;
typedef int ALsizei;
typedef void ALvoid;
typedef float ALfloat;

#define AL_NO_ERROR 0
#define AL_FALSE 0
#define AL_TRUE 1
#define AL_INVALID_NAME 0xA001
#define AL_INVALID_ENUM 0xA002
#define AL_INVALID_VALUE 0xA003
#define AL_INVALID_OPERATION 0xA004
#define AL_OUT_OF_MEMORY 0xA005

#define AL_SOURCE_STATE 0x1010
#define AL_PLAYING 0x1012
#define AL_PAUSED 0x1013
#define AL_STOPPED 0x1014
#define AL_BUFFERS_PROCESSED 0x1016
#define AL_BUFFER 0x1009
#define AL_FORMAT_MONO8 0x1100
#define AL_FORMAT_MONO16 0x1101
#define AL_FORMAT_STEREO8 0x1102
#define AL_FORMAT_STEREO16 0x1103
#define AL_FREQUENCY 0x1007
#define AL_BITS 0x1008
#define AL_CHANNELS 0x1006
#define AL_SIZE 0x1004
#define AL_POSITION 0x1003
#define AL_GAIN 0x100A
#define AL_PITCH 0x1003

static inline void alGenSources(ALsizei n, ALuint* sources) {}
static inline void alDeleteSources(ALsizei n, const ALuint* sources) {}
static inline void alGenBuffers(ALsizei n, ALuint* buffers) {}
static inline void alDeleteBuffers(ALsizei n, const ALuint* buffers) {}
static inline void alSourcePlay(ALuint source) {}
static inline void alSourcePause(ALuint source) {}
static inline void alSourceStop(ALuint source) {}
static inline void alSourceRewind(ALuint source) {}
static inline void alSourceQueueBuffers(ALuint source, ALsizei n, const ALuint* buffers) {}
static inline void alSourceUnqueueBuffers(ALuint source, ALsizei n, ALuint* buffers) {}
static inline void alBufferData(ALuint buffer, ALenum format, const ALvoid* data, ALsizei size, ALsizei freq) {}
static inline void alSourcei(ALuint source, ALenum param, ALint value) {}
static inline void alSourcef(ALuint source, ALenum param, ALfloat value) {}
static inline void alSource3f(ALuint source, ALenum param, ALfloat v1, ALfloat v2, ALfloat v3) {}
static inline void alGetSourcei(ALuint source, ALenum param, ALint* value) { *value = 0; }
static inline ALenum alGetError(void) { return AL_NO_ERROR; }
static inline void alListenerf(ALenum param, ALfloat value) {}
static inline void alListener3f(ALenum param, ALfloat v1, ALfloat v2, ALfloat v3) {}
static inline const ALchar* alGetString(ALenum param) { return (const ALchar*)""; }
static inline void alDistanceModel(ALenum model) {}
static inline void alDopplerFactor(ALfloat factor) {}
