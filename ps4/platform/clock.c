/* OpenOrbis libc++ was built with Linux CLOCK_MONOTONIC=1, whereas
 * libkernel uses the FreeBSD ABI: 1=VIRTUAL (CPU time), 4=MONOTONIC.
 * Translate this SDK request at the import boundary, including libc++'s
 * already-compiled steady_clock::now. Realtime (0) stays unchanged.
 * Native sceKernel clock APIs bypass this wrapper. */
#include <time.h>
extern int __real_clock_gettime(clockid_t, struct timespec *);
int __wrap_clock_gettime(clockid_t clock, struct timespec *value) {
    return __real_clock_gettime(clock == CLOCK_MONOTONIC ? 4 : clock, value);
}
