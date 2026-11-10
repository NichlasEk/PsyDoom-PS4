// SPDX-License-Identifier: GPL-3.0-or-later
// OpenOrbis's installed bits/alltypes.h declares mode_t twice. The first
// definition is uint32, but the PS4 kernel stat ABI requires uint16. Load
// the correct type before any SDK header so all application TUs agree.
#ifndef SCUMMVM_PS4_ABI_H
#define SCUMMVM_PS4_ABI_H
#ifdef __PS4__
#ifndef __DEFINED_mode_t
typedef unsigned short mode_t;
#define __DEFINED_mode_t
#endif
#include <stddef.h>
#include <sys/stat.h>
#ifdef __cplusplus
static_assert(sizeof(mode_t) == 2, "PS4 mode_t must be 16-bit");
static_assert(offsetof(struct stat, st_size) == 72, "PS4 stat size offset mismatch");
static_assert(offsetof(struct stat, st_blocks) == 80, "PS4 stat blocks offset mismatch");
static_assert(sizeof(struct stat) == 120, "PS4 stat ABI size mismatch");
#endif
#endif
#endif
/* FreeBSD socket ioctl request ABI (not Linux ioctl numbers). */
#if defined(__PS4__)
#ifndef FIONBIO
#define FIONBIO 0x8004667eUL
#endif
#ifndef FIONREAD
#define FIONREAD 0x4004667fUL
#endif
#endif
