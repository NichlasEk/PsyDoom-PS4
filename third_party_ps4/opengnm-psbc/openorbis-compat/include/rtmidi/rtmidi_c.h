#pragma once
// PS4 stub for rtmidi_c.h (RtMidi not available on PS4)

#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int RtMidiApi;
typedef int RtMidiErrorType;

#define RTMIDI_API_UNSPECIFIED 0
#define RTMIDI_API_MACOSX_CORE 1
#define RTMIDI_API_LINUX_ALSA  2
#define RTMIDI_API_UNIX_JACK   3
#define RTMIDI_API_WINDOWS_MM  4
#define RTMIDI_API_RTMIDI_DUMMY 5

typedef struct RtMidiIn {
    void* ptr_;
    RtMidiApi api;
    void (*callback)(double, const unsigned char*, size_t, void*);
    void* userData;
    int ok;
    char msg[256];
} RtMidiIn;

typedef struct RtMidiOut {
    void* ptr_;
    RtMidiApi api;
    int ok;
    char msg[256];
} RtMidiOut;

typedef RtMidiIn* RtMidiInPtr;
typedef RtMidiOut* RtMidiOutPtr;

typedef void (*RtMidiCCallback)(double timeStamp, const unsigned char* message, size_t messageSize, void* userData);

static inline RtMidiIn* rtmidi_in_create_default(void) { return nullptr; }
static inline RtMidiIn* rtmidi_in_create(RtMidiApi api, const char* clientName, unsigned int queueSizeLimit) { return nullptr; }
static inline void rtmidi_in_free(RtMidiIn* in) {}
static inline void rtmidi_in_set_callback(RtMidiIn* in, RtMidiCCallback cb, void* data) {}
static inline void rtmidi_in_cancel_callback(RtMidiIn* in) {}
static inline unsigned int rtmidi_in_get_port_count(RtMidiIn* in) { return 0; }
static inline const char* rtmidi_in_get_port_name(RtMidiIn* in, unsigned int port) { return ""; }
static inline void rtmidi_in_open_port(RtMidiIn* in, unsigned int port, const char* name) {}
static inline void rtmidi_in_open_virtual_port(RtMidiIn* in, const char* name) {}
static inline void rtmidi_in_close_port(RtMidiIn* in) {}
static inline void rtmidi_in_ignore_types(RtMidiIn* in, bool midiSysex, bool midiTime, bool midiSense) {}
static inline double rtmidi_in_get_message(RtMidiIn* in, unsigned char* message, size_t* size) { *size = 0; return 0.0; }
static inline RtMidiOut* rtmidi_out_create_default(void) { return nullptr; }
static inline RtMidiOut* rtmidi_out_create(RtMidiApi api, const char* clientName) { return nullptr; }
static inline void rtmidi_out_free(RtMidiOut* out) {}
static inline unsigned int rtmidi_out_get_port_count(RtMidiOut* out) { return 0; }
static inline const char* rtmidi_out_get_port_name(RtMidiOut* out, unsigned int port) { return ""; }
static inline void rtmidi_out_open_port(RtMidiOut* out, unsigned int port, const char* name) {}
static inline void rtmidi_out_open_virtual_port(RtMidiOut* out, const char* name) {}
static inline void rtmidi_out_close_port(RtMidiOut* out) {}
static inline void rtmidi_out_send_message(RtMidiOut* out, const unsigned char* msg, size_t size) {}

// Generic functions (work on both RtMidiIn and RtMidiOut)
static inline unsigned int rtmidi_get_port_count(RtMidiIn* device) { return rtmidi_in_get_port_count(device); }
static inline unsigned int rtmidi_get_port_count(RtMidiOut* device) { return rtmidi_out_get_port_count(device); }
static inline int rtmidi_get_port_name(RtMidiIn* device, unsigned int port, char* buf, int* size) { if (size) *size = 0; return 0; }
static inline int rtmidi_get_port_name(RtMidiOut* device, unsigned int port, char* buf, int* size) { if (size) *size = 0; return 0; }
static inline void rtmidi_open_port(RtMidiIn* device, unsigned int port, const char* name) { rtmidi_in_open_port(device, port, name); }
static inline void rtmidi_open_port(RtMidiOut* device, unsigned int port, const char* name) { rtmidi_out_open_port(device, port, name); }
static inline void rtmidi_close_port(RtMidiIn* device) { rtmidi_in_close_port(device); }
static inline void rtmidi_close_port(RtMidiOut* device) { rtmidi_out_close_port(device); }
static inline void rtmidi_open_virtual_port(RtMidiIn* device, const char* name) { rtmidi_in_open_virtual_port(device, name); }
static inline void rtmidi_open_virtual_port(RtMidiOut* device, const char* name) { rtmidi_out_open_virtual_port(device, name); }

static inline const char* rtmidi_api_name(RtMidiApi api) { return "dummy"; }
static inline const char* rtmidi_api_display_name(RtMidiApi api) { return "Dummy"; }
static inline RtMidiApi rtmidi_in_get_current_api(RtMidiIn* in) { return RTMIDI_API_RTMIDI_DUMMY; }
static inline RtMidiApi rtmidi_out_get_current_api(RtMidiOut* out) { return RTMIDI_API_RTMIDI_DUMMY; }

#ifdef __cplusplus
}
#endif
