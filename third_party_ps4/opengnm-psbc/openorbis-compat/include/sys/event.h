#pragma once

// PS4 sys/event.h — kqueue is available via syscall on PS4 (FreeBSD kernel)

#include <sys/types.h>
#include <bits/syscall.h>
#include <unistd.h>

struct kevent {
    uintptr_t ident;
    short filter;
    unsigned short flags;
    unsigned int fflags;
    intptr_t data;
    void* udata;
};

#define EVFILT_READ     (-1)
#define EVFILT_WRITE    (-2)
#define EVFILT_AIO      (-3)
#define EVFILT_VNODE    (-4)
#define EVFILT_PROC     (-5)
#define EVFILT_SIGNAL   (-6)
#define EVFILT_TIMER    (-7)
#define EVFILT_USER     (-11)

#define EV_ADD      0x0001
#define EV_DELETE   0x0002
#define EV_ENABLE   0x0004
#define EV_DISABLE  0x0008
#define EV_ONESHOT  0x0010
#define EV_CLEAR    0x0020
#define EV_RECEIPT  0x0040
#define EV_DISPATCH 0x0080

#define NOTE_DELETE 0x0001
#define NOTE_WRITE  0x0002
#define NOTE_EXTEND 0x0004
#define NOTE_ATTRIB 0x0008
#define NOTE_LINK   0x0010
#define NOTE_RENAME 0x0020
#define NOTE_REVOKE 0x0040
#define NOTE_FFCTRLMASK 0xc0000000
#define NOTE_FFLAGSMASK 0x00ffffff
#define NOTE_FFNOP     0x00000000
#define NOTE_FFAND     0x40000000
#define NOTE_FFOR      0x80000000
#define NOTE_FFCOPY    0xc0000000
#define NOTE_TRIGGER   0x01000000
#define NOTE_NSECONDS  0x00000002
#define NOTE_CRITICAL  0x00000004

#define EV_SET(_kevp, _ident, _filter, _flags, _fflags, _data, _udata) do { \
    (_kevp)->ident = (_ident);      \
    (_kevp)->filter = (_filter);    \
    (_kevp)->flags = (_flags);      \
    (_kevp)->fflags = (_fflags);    \
    (_kevp)->data = (_data);        \
    (_kevp)->udata = (void*)(_udata); \
} while (0)

// PS4 has kqueue/kevent syscalls (FreeBSD kernel), use them directly.
static inline int kqueue(void) {
    return (int)syscall(__NR_kqueue);
}

static inline int kevent(int kq, const struct kevent* changelist, int nchanges,
                         struct kevent* eventlist, int nevents,
                         const void* timeout) {
    return (int)syscall(__NR_kevent, kq, changelist, nchanges, eventlist, nevents, timeout);
}
