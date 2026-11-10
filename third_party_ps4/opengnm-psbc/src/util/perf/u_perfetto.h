#ifndef _UTIL_PERFETTO_H
#define _UTIL_PERFETTO_H

#include "util/u_atomic.h"
#include "util/detect_os.h"

typedef int perfetto_clock_id;

#if defined(__cplusplus) && defined(HAVE_PERFETTO)
#include <perfetto.h>
#include <perfetto/tracing.h>
#endif

#ifdef HAVE_PERFETTO
extern int util_perfetto_tracing_state;
void util_perfetto_thread_flush(void);
#else
static inline int util_perfetto_is_tracing_enabled(void) { return 0; }
static inline void util_perfetto_thread_flush(void) {}
static inline uint32_t util_perfetto_next_id(void) { return 0; }
#endif

#endif
