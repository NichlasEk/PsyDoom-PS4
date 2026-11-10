#pragma once

// PS4 stub for execinfo.h (backtrace not available on PS4)

static inline int backtrace(void** buffer, int size) { return 0; }
static inline char** backtrace_symbols(void* const* buffer, int size) { return 0; }
static inline void backtrace_symbols_fd(void* const* buffer, int size, int fd) {}
