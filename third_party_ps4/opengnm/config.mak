DESTDIR=/usr/local
BINDIR=/bin
INCDIR=/include
LIBDIR=/lib

LIBEXT=.so
LIBVER=0.1.0

# the platform to build the library on
# possible values are:
# * generic - Build for a platform without a GNM driver (host testing)
# * orbis - Build for the Playstation 4 Orbis OS
PLATFORM=orbis

TOOLCHAIN=$(OO_PS4_TOOLCHAIN)

AR?=ar
CC?=clang
LD?=ld.lld
CFLAGS=-std=c11 -Wall -Wextra -Wpedantic -I./include -I./src -O2 -g \
    --target=x86_64-ps4-elf -fPIC \
    -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include
LDFLAGS=-m elf_x86_64 -L$(TOOLCHAIN)/lib -lc -lkernel -lSceGnmDriver -lSceVideoOut -L. -lopengnm
LIB_LDFLAGS=-shared -m elf_x86_64 -L$(TOOLCHAIN)/lib -lc -lkernel -lSceGnmDriver -lSceVideoOut -L. -lopengnm
