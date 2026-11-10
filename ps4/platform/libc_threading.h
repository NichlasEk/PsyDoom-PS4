#ifndef PS4LIBC_THREADING_H
#define PS4LIBC_THREADING_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sched.h>
#include <pthread.h>
#include <orbis/libkernel.h>
/* OpenOrbis links musl statically but imports pthread_create from libkernel.
 * The imported function does not update musl's private lock gate. SDK malloc
 * and __lock test the fourth int in __libc; without it they skip locking.
 * ABI prefix: https://github.com/OpenOrbis/musl/blob/master/src/internal/libc.h
 * Checked against this SDK's disassembly and the crashing process core.
 * Call once, before creating threads; never reset while threads may exist. */
struct PS4MuslPrefix {
  int can_do_threads, threaded, secure;
  volatile int threads_minus_1;
};
extern struct PS4MuslPrefix __libc;
_Static_assert(offsetof(struct PS4MuslPrefix, threads_minus_1)==12, "OpenOrbis musl lock ABI");
/* Kernel attributes are pointer handles, while this SDK aliases the public
 * typedef to musl's four-byte structure. Reserve pointer-sized aligned storage. */
static OrbisPthreadMutex allocator_mutex;
static int allocator_ready;
static inline void ps4EnableLibcLocks(void) {
  union { OrbisPthreadMutexattr attr; void *handle; } storage={0};
  __libc.threads_minus_1=1;
  if (scePthreadMutexattrInit(&storage.attr) ||
      scePthreadMutexattrSettype(&storage.attr,PTHREAD_MUTEX_RECURSIVE) ||
      scePthreadMutexInit(&allocator_mutex,&storage.attr,"scummvm-allocator")) abort();
  scePthreadMutexattrDestroy(&storage.attr);
  allocator_ready=1;
}
/* Serialize libc entry across kernel-created graphics/audio threads. Recursive
 * ownership permits libc-internal calls redirected by the linker's --wrap.
 * Startup before main remains single-threaded and uses the real allocator. */
extern void *__real_malloc(size_t);
extern void *__real_calloc(size_t,size_t);
extern void *__real_realloc(void *,size_t);
extern void __real_free(void *);
static void allocator_lock(void) {
  if (allocator_ready && scePthreadMutexLock(&allocator_mutex)) abort();
}
static void allocator_unlock(void) {
  if (allocator_ready && scePthreadMutexUnlock(&allocator_mutex)) abort();
}
#ifdef PS4_LARGE_DIRECT_HEAP
#include "ps4libc-direct-heap.h"
#endif
void *__wrap_malloc(size_t bytes) {
#ifdef PS4_LARGE_DIRECT_HEAP
  allocator_lock(); void *p=ps4_heap_alloc(bytes); allocator_unlock(); return p;
#else
  allocator_lock(); void *p=__real_malloc(bytes); allocator_unlock(); return p;
#endif
}
void *__wrap_calloc(size_t count,size_t bytes) {
#ifdef PS4_LARGE_DIRECT_HEAP
  if (bytes && count>SIZE_MAX/bytes) { errno=ENOMEM; return NULL; }
  allocator_lock(); void *p=ps4_heap_alloc(count*bytes);
  if (p) memset(p,0,count*bytes);
  allocator_unlock(); return p;
#else
  allocator_lock(); void *p=__real_calloc(count,bytes); allocator_unlock(); return p;
#endif
}
void *__wrap_realloc(void *old,size_t bytes) {
  allocator_lock(); void *p=
#ifdef PS4_LARGE_DIRECT_HEAP
  ps4_heap_realloc(old,bytes);
#else
  __real_realloc(old,bytes);
#endif
 allocator_unlock(); return p;
}
void __wrap_free(void *p) {
  allocator_lock();
#ifdef PS4_LARGE_DIRECT_HEAP
  struct PS4DirectAllocation *a=p ? ps4_direct_find(p) : NULL;
  if (a) ps4_direct_free(a); else __real_free(p);
#else
  __real_free(p);
#endif
  allocator_unlock();
}
#endif
