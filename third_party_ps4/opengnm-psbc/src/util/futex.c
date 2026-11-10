/*
 * Copyright © 2015 Intel
 * Copyright © 2022 Yonggang Luo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "util/futex.h"

#if UTIL_FUTEX_SUPPORTED

#if defined(HAVE_LINUX_FUTEX_H)

#include <limits.h>
#include <stdint.h>
#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>

#ifndef SYS_futex
#define SYS_futex SYS_futex_time64
#endif

static inline long sys_futex(void *addr1, int op, int val1, const struct timespec *timeout, void *addr2, int val3)
{
   return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}

int futex_wake(uint32_t *addr, int32_t count)
{
   return sys_futex(addr, FUTEX_WAKE, count, NULL, NULL, 0);
}

int futex_wait(uint32_t *addr, int32_t value, const struct timespec *timeout)
{
   /* FUTEX_WAIT_BITSET with FUTEX_BITSET_MATCH_ANY is equivalent to
    * FUTEX_WAIT, except that it treats the timeout as absolute. */
   return sys_futex(addr, FUTEX_WAIT_BITSET, value, timeout, NULL,
                    FUTEX_BITSET_MATCH_ANY);
}

#elif defined(__ORBIS__)

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

/* OpenOrbis does not expose FreeBSD's <sys/umtx.h>. Use a process-local
 * condition variable as the futex backend. Waiters check the futex value
 * while holding the mutex, so a wake cannot race with condition registration. */
static pthread_mutex_t orbis_futex_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t orbis_futex_cond;
static pthread_once_t orbis_futex_once = PTHREAD_ONCE_INIT;

static void
orbis_futex_init(void)
{
   pthread_condattr_t attr;
   pthread_condattr_init(&attr);
   pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
   pthread_cond_init(&orbis_futex_cond, &attr);
   pthread_condattr_destroy(&attr);
}

static void
orbis_futex_ensure_initialized(void)
{
   pthread_once(&orbis_futex_once, orbis_futex_init);
}

int futex_wake(uint32_t *addr, int32_t count)
{
   (void)addr;
   if (count <= 0)
      return 0;

   orbis_futex_ensure_initialized();
   pthread_mutex_lock(&orbis_futex_mutex);
   if (count == 1)
      pthread_cond_signal(&orbis_futex_cond);
   else
      pthread_cond_broadcast(&orbis_futex_cond);
   pthread_mutex_unlock(&orbis_futex_mutex);
   return count;
}

int futex_wait(uint32_t *addr, int32_t value, const struct timespec *timeout)
{
   int result = 0;

   orbis_futex_ensure_initialized();
   pthread_mutex_lock(&orbis_futex_mutex);
   while (__atomic_load_n(addr, __ATOMIC_ACQUIRE) == (uint32_t)value) {
      if (timeout) {
         result = pthread_cond_timedwait(&orbis_futex_cond,
                                         &orbis_futex_mutex, timeout);
         if (result != 0)
            break;
      } else {
         result = pthread_cond_wait(&orbis_futex_cond,
                                    &orbis_futex_mutex);
         if (result != 0)
            break;
      }
   }
   pthread_mutex_unlock(&orbis_futex_mutex);

   /* If the value changed before we noticed the timeout, treat as success. */
   if (result == ETIMEDOUT &&
       __atomic_load_n(addr, __ATOMIC_ACQUIRE) != (uint32_t)value)
      return 0;

   if (result != 0) {
      errno = result;
      return -1;
   }
   return 0;
}

#elif defined(__FreeBSD__)

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/umtx.h>

int futex_wake(uint32_t *addr, int32_t count)
{
   assert(count >= 0);
   return _umtx_op(addr, UMTX_OP_WAKE, (uint32_t)count, NULL, NULL) == -1 ? errno : 0;
}

int futex_wait(uint32_t *addr, int32_t value, const struct timespec *timeout)
{
   void *uaddr = NULL, *uaddr2 = NULL;
   struct _umtx_time tmo = {
      ._flags = UMTX_ABSTIME,
      ._clockid = CLOCK_MONOTONIC
   };

   assert(value == (int)(uint32_t)value); /* Check that bits weren't discarded */

   if (timeout != NULL) {
      tmo._timeout = *timeout;
      uaddr = (void *)(uintptr_t)sizeof(tmo);
      uaddr2 = (void *)&tmo;
   }

   return _umtx_op(addr, UMTX_OP_WAIT_UINT, (uint32_t)value, uaddr, uaddr2) == -1 ? errno : 0;
}

#elif defined(__OpenBSD__)

#include <sys/futex.h>
#include <sys/time.h>

int futex_wake(uint32_t *addr, int32_t count)
{
   return futex(addr, FUTEX_WAKE, count, NULL, NULL);
}

int futex_wait(uint32_t *addr, int32_t value, const struct timespec *timeout)
{
   struct timespec tsnow, tsrel;

   if (timeout == NULL)
      return futex(addr, FUTEX_WAIT, value, NULL, NULL);

   clock_gettime(CLOCK_MONOTONIC, &tsnow);
   if (timespeccmp(&tsnow, timeout, <))
      timespecsub(timeout, &tsnow, &tsrel);
   else
      timespecclear(&tsrel);
   return futex(addr, FUTEX_WAIT, value, &tsrel, NULL);
}

#elif defined(_WIN32) && !defined(WINDOWS_NO_FUTEX)

#include <windows.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include <errno.h>

int futex_wake(uint32_t *addr, int32_t count)
{
   /* All current callers fall into one of these buckets, and we'll get the semantics
    * wrong if someone tries to be more clever.
    */
   assert(count == 1 || count == INT32_MAX);
   if (count == 1)
      WakeByAddressSingle(addr);
   else
      WakeByAddressAll(addr);
   return count;
}

int futex_wait(uint32_t *addr, int32_t value, const struct timespec *timeout)
{
   DWORD timeout_ms = INFINITE;
   if (timeout != NULL) {
      struct timespec tsnow;
      timespec_get(&tsnow, TIME_MONOTONIC);

      timeout_ms = (timeout->tv_sec - tsnow.tv_sec) * 1000 +
                   (timeout->tv_nsec - tsnow.tv_nsec) / 1000000;
   }

   if (WaitOnAddress(addr, &value, sizeof(value), timeout_ms))
      return 0;
   return GetLastError() == ERROR_TIMEOUT ? ETIMEDOUT : -1;
}

#else
#error UTIL_FUTEX_SUPPORTED is not implemented but the header told it is supported on this platform
#endif

#endif /* UTIL_FUTEX_SUPPORTED */
