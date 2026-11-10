#pragma once
// Stub libavcodec/avcodec.h for PS4 (ffmpeg not available)
#include "../libavutil/pixfmt.h"
#include "../libavutil/rational.h"
#include "../libavutil/dict.h"

#ifdef __cplusplus
extern "C" {
#endif

enum AVCodecID {
    AV_CODEC_ID_NONE = 0,
    AV_CODEC_ID_MPEG2VIDEO = 2,
    AV_CODEC_ID_H264 = 28,
    AV_CODEC_ID_MPEG4 = 15,
    AV_CODEC_ID_AAC = 0x15002,
    AV_CODEC_ID_AC3 = 0x15003,
    AV_CODEC_ID_ATRAC3 = 0x15001,
    AV_CODEC_ID_ATRAC3P = 0x15002,
    AV_CODEC_ID_PCM_S16LE = 0x10001,
};

enum AVMediaType {
    AVMEDIA_TYPE_UNKNOWN = -1,
    AVMEDIA_TYPE_VIDEO = 0,
    AVMEDIA_TYPE_AUDIO = 1,
    AVMEDIA_TYPE_DATA = 2,
    AVMEDIA_TYPE_SUBTITLE = 3,
};

enum AVDiscard {
    AVDISCARD_NONE = -16,
    AVDISCARD_DEFAULT = 0,
    AVDISCARD_NONREF = 8,
    AVDISCARD_BIDIR = 16,
    AVDISCARD_NONINTRA = 24,
    AVDISCARD_NONKEY = 32,
    AVDISCARD_ALL = 48,
};

typedef struct AVCodec {
    enum AVCodecID id;
    enum AVMediaType type;
    const char* name;
    const char* long_name;
} AVCodec;

typedef struct AVCodecDescriptor {
    enum AVCodecID id;
    enum AVMediaType type;
    const char* name;
    const char* long_name;
} AVCodecDescriptor;

typedef struct AVCodecParameters AVCodecParameters;

// AVChannelLayout stub
typedef struct AVChannelLayout {
    int nb_channels;
    int order;
    void* u;
} AVChannelLayout;

typedef struct AVCodecContext {
    const AVCodec* codec;
    enum AVMediaType codec_type;
    enum AVCodecID codec_id;
    int thread_count;
    int thread_type;
    int width;
    int height;
    int sample_rate;
    int channels;
    int64_t bit_rate;
    int frame_size;
    int block_align;
    void* opaque;
    int (*get_buffer2)(struct AVCodecContext* ctx, struct AVFrame* frame, int flags);
    AVChannelLayout ch_layout;
    int skip_frame;
    int ticks_per_frame;
    int pix_fmt;
    AVRational framerate;
    AVRational time_base;
} AVCodecContext;

typedef struct AVBufferRef {
    uint8_t* data;
    int size;
} AVBufferRef;

typedef struct AVPacket {
    int64_t pts;
    int64_t dts;
    uint8_t* data;
    int size;
    int stream_index;
    int flags;
    int64_t pos;
    AVBufferRef* buf;
} AVPacket;

typedef struct AVFrame {
    uint8_t* data[8];
    int linesize[8];
    int width;
    int height;
    int nb_samples;
    int format;
    int key_frame;
    int64_t pts;
    int64_t pkt_dts;
    int interlaced_frame;
    int repeat_pict;
    int pict_type;
    AVBufferRef* buf[8];
    AVChannelLayout ch_layout;
} AVFrame;

#define AVERROR_EOF (-0x20464F45)
#define AVERROR_INVALIDDATA (-0x49444154)
#define AVERROR_AGAIN (-0x41474149)

#define AV_PICTURE_TYPE_NONE 0
#define AV_PICTURE_TYPE_I 1
#define AV_PICTURE_TYPE_P 2
#define AV_PICTURE_TYPE_B 3
#define AV_PICTURE_TYPE_S 4
#define AV_PICTURE_TYPE_SI 5
#define AV_PICTURE_TYPE_SP 6
#define AV_PICTURE_TYPE_BI 7
#define AVERROR(E) (-E)

#define FF_THREAD_SLICE 2

// Version macros
#define AV_VERSION_INT(a, b, c) (((a) << 16) | ((b) << 8) | (c))
#define LIBAVCODEC_VERSION_INT AV_VERSION_INT(60, 0, 0)

static inline AVFrame* av_frame_alloc(void) { return nullptr; }
static inline void av_frame_free(AVFrame** frame) {}
static inline void av_frame_unref(AVFrame* frame) {}
static inline AVPacket* av_packet_alloc(void) { return nullptr; }
static inline void av_packet_free(AVPacket** pkt) {}
static inline void av_packet_unref(AVPacket* pkt) {}
static inline AVBufferRef* av_buffer_create(uint8_t* data, int size, void (*freefn)(void*, uint8_t*), void* opaque, int flags) { return nullptr; }
static inline void av_buffer_unref(AVBufferRef** buf) {}
static inline AVCodecContext* avcodec_alloc_context3(const AVCodec* codec) { return nullptr; }
static inline void avcodec_free_context(AVCodecContext** ctx) {}
static inline const AVCodec* avcodec_find_decoder(enum AVCodecID id) { return nullptr; }
static inline const AVCodecDescriptor* avcodec_descriptor_get(enum AVCodecID id) { return nullptr; }
static inline int avcodec_open2(AVCodecContext* ctx, const AVCodec* codec, void* opts) { return -1; }
static inline void avcodec_close(AVCodecContext* ctx) {}
static inline void avcodec_flush_buffers(AVCodecContext* ctx) {}
static inline int avcodec_send_packet(AVCodecContext* ctx, const AVPacket* pkt) { return -1; }
static inline int avcodec_receive_frame(AVCodecContext* ctx, AVFrame* frame) { return -1; }
static inline int avcodec_decode_audio4(AVCodecContext* ctx, AVFrame* frame, int* got, const AVPacket* pkt) { return -1; }
static inline int avcodec_decode_video2(AVCodecContext* ctx, AVFrame* frame, int* got, const AVPacket* pkt) { return -1; }
static inline void av_init_packet(AVPacket* pkt) {}
static inline unsigned avcodec_version(void) { return 0; }
static inline const char* avcodec_configuration(void) { return "stub"; }

#ifdef __cplusplus
}
#endif
