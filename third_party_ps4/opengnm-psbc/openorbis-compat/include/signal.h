#pragma once

// PS4 compat: fix broken sa_sigaction macro in SDK signal.h.
// The SDK defines #define sa_sigaction __sa_handler.sa_sigaction
// but the actual union member is __sa_sigaction (with __ prefix).
// This header includes the real signal.h and fixes the macro.

// Undef the broken macro
#ifdef sa_sigaction
#undef sa_sigaction
#endif

// Include the real signal.h
#include_next <signal.h>

// Fix the macro: __sa_handler has member __sa_sigaction, not sa_sigaction
#ifdef sa_sigaction
#undef sa_sigaction
#endif
#define sa_sigaction __sa_handler.__sa_sigaction

// Also fix sa_handler if needed
#ifdef sa_handler
#undef sa_handler
#endif
#define sa_handler __sa_handler.sa_handler
