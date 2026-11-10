#pragma once

#include_next <netinet/in.h>

#if defined(__OPENORBIS__) && !defined(__u6_addr)
#define __u6_addr __in6_union
#define __u6_addr8 __s6_addr
#define __u6_addr16 __s6_addr16
#define __u6_addr32 __s6_addr32
#endif
