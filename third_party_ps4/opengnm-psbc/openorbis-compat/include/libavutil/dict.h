#pragma once
// Stub libavutil/dict.h for PS4 (ffmpeg not available)
typedef struct AVDictionary AVDictionary;

typedef struct AVDictionaryEntry {
    char* key;
    char* value;
} AVDictionaryEntry;

#define AV_DICT_IGNORE_SUFFIX 2

static inline AVDictionaryEntry* av_dict_get(const AVDictionary* m, const char* key, const AVDictionaryEntry* prev, int flags) { return nullptr; }
static inline void av_dict_free(AVDictionary** m) {}

