#pragma once

// PS4 stub for wolfssl/ssl.h (wolfSSL not available on PS4)

typedef struct WOLFSSL WOLFSSL;
typedef struct WOLFSSL_CTX WOLFSSL_CTX;

// Error codes
#define SSL_SUCCESS 0
#define SSL_FAILURE -1
#define SSL_ERROR_WANT_READ -2
#define SSL_ERROR_WANT_WRITE -3

// Stub functions
static inline WOLFSSL_CTX* wolfSSL_CTX_new(int method) { return nullptr; }
static inline void wolfSSL_CTX_free(WOLFSSL_CTX* ctx) {}
static inline WOLFSSL* wolfSSL_new(WOLFSSL_CTX* ctx) { return nullptr; }
static inline void wolfSSL_free(WOLFSSL* ssl) {}
static inline int wolfSSL_connect(WOLFSSL* ssl) { return SSL_FAILURE; }
static inline int wolfSSL_read(WOLFSSL* ssl, void* buf, int sz) { return -1; }
static inline int wolfSSL_write(WOLFSSL* ssl, const void* buf, int sz) { return -1; }
static inline int wolfSSL_get_error(WOLFSSL* ssl, int ret) { return -1; }
static inline const char* wolfSSL_ERR_error_string(unsigned long e, char* buf) { return "stub"; }
static inline void wolfSSL_Init(void) {}
static inline void wolfSSL_Cleanup(void) {}
static inline int wolfSSL_CTX_set_cipher_list(WOLFSSL_CTX* ctx, const char* list) { return SSL_FAILURE; }
static inline int wolfSSL_CTX_use_certificate_file(WOLFSSL_CTX* ctx, const char* f, int t) { return SSL_FAILURE; }
static inline int wolfSSL_CTX_use_PrivateKey_file(WOLFSSL_CTX* ctx, const char* f, int t) { return SSL_FAILURE; }
static inline int wolfSSL_load_error_strings(void) { return 0; }
static inline int wolfSSL_set_fd(WOLFSSL* ssl, int fd) { return SSL_FAILURE; }

// TLS method
#define WOLFSSL_TLSV1_2_CLIENT_VERSION 0
typedef int WOLFSSL_METHOD;
static inline const WOLFSSL_METHOD* wolfTLSv1_2_client_method(void) { return nullptr; }
