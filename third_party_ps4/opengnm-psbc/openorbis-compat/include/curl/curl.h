#pragma once
// PS4 stub for curl/curl.h (libcurl not available on PS4)

#include <stddef.h>
#include <stdint.h>

typedef void CURL;
typedef struct CURLM CURLM;
typedef struct curl_slist {
    char* data;
    struct curl_slist* next;
} curl_slist;

typedef enum {
    CURLE_OK = 0,
    CURLE_FAILED_INIT = 2,
    CURLE_NOT_BUILT_IN = 4,
    CURLE_COULDNT_RESOLVE_HOST = 6,
    CURLE_COULDNT_CONNECT = 7,
    CURLE_OPERATION_TIMEDOUT = 28,
} CURLcode;

typedef enum {
    CURLM_OK = 0,
} CURLMcode;

#define CURLOPTTYPE_LONG 0
#define CURLOPTTYPE_OBJECTPOINT 10000
#define CURLOPTTYPE_FUNCTIONPOINT 20000
#define CURLOPTTYPE_OFF_T 30000

#define CURLOPT_URL CURLOPTTYPE_OBJECTPOINT + 2
#define CURLOPT_WRITEFUNCTION CURLOPTTYPE_FUNCTIONPOINT + 11
#define CURLOPT_WRITEDATA CURLOPTTYPE_OBJECTPOINT + 1
#define CURLOPT_HTTPHEADER CURLOPTTYPE_OBJECTPOINT + 23
#define CURLOPT_POSTFIELDS CURLOPTTYPE_OBJECTPOINT + 15
#define CURLOPT_POSTFIELDSIZE CURLOPTTYPE_LONG + 60
#define CURLOPT_TIMEOUT CURLOPTTYPE_LONG + 13
#define CURLOPT_CONNECTTIMEOUT CURLOPTTYPE_LONG + 78
#define CURLOPT_SSL_VERIFYPEER CURLOPTTYPE_LONG + 64
#define CURLOPT_SSL_VERIFYHOST CURLOPTTYPE_LONG + 81
#define CURLOPT_FOLLOWLOCATION CURLOPTTYPE_LONG + 52
#define CURLOPT_NOSIGNAL CURLOPTTYPE_LONG + 99
#define CURLOPT_USERAGENT CURLOPTTYPE_OBJECTPOINT + 18
#define CURLOPT_NOBODY CURLOPTTYPE_LONG + 44
#define CURLOPT_HEADER CURLOPTTYPE_LONG + 42
#define CURLOPT_CUSTOMREQUEST CURLOPTTYPE_OBJECTPOINT + 36
#define CURLOPT_RANGE CURLOPTTYPE_OBJECTPOINT + 7

#define CURLINFO_LONG 0x100000
#define CURLINFO_STRING 0x100001
#define CURLINFO_RESPONSE_CODE CURLINFO_LONG + 2

static inline CURL* curl_easy_init(void) { return nullptr; }
static inline void curl_easy_cleanup(CURL* handle) {}
static inline CURLcode curl_easy_setopt(CURL* handle, int opt, ...) { return CURLE_NOT_BUILT_IN; }
static inline CURLcode curl_easy_perform(CURL* handle) { return CURLE_NOT_BUILT_IN; }
static inline CURLcode curl_easy_getinfo(CURL* handle, int info, ...) { return CURLE_NOT_BUILT_IN; }
static inline void curl_easy_reset(CURL* handle) {}
static inline const char* curl_easy_strerror(CURLcode code) { return "stub"; }
static inline curl_slist* curl_slist_append(curl_slist* list, const char* str) { return nullptr; }
static inline void curl_slist_free_all(curl_slist* list) {}
static inline CURLM* curl_multi_init(void) { return nullptr; }
static inline CURLMcode curl_multi_add_handle(CURLM* mh, CURL* eh) { return CURLM_OK; }
static inline CURLMcode curl_multi_remove_handle(CURLM* mh, CURL* eh) { return CURLM_OK; }
static inline CURLMcode curl_multi_perform(CURLM* mh, int* running) { *running = 0; return CURLM_OK; }
static inline void curl_multi_cleanup(CURLM* mh) {}
static inline const char* curl_version(void) { return "stub"; }
