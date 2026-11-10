# config.orbis.mak — PS4 (OpenOrbis) cross-compile configuration for opengnm-psbc
#
# Usage:
#   export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis-PS4-Toolchain
#   make -f Makefile.orbis
#
# This builds libpsbc.a for PS4 (FreeBSD x86_64). The CLI (opengnm-psbc)
# is not built — it's a host tool. Only the static library is produced.

DESTDIR=/usr/local
BINDIR=/bin

# Path to opengnm headers (for GnmShaderFileHeader, GnmVsShader, etc.)
OPENGNM_INCLUDE?=../opengnm/include

SHARED_FLAGS=\
	-Iinclude/ \
	-Ilibpsbc/ \
	-I$(OPENGNM_INCLUDE) \
	-I../Vulkan-Headers/include \
	-Isrc/ \
	-Isrc/amd \
	-Isrc/amd/common \
	-Isrc/amd/common/nir \
	-Isrc/amd/compiler \
	-Isrc/amd/vulkan \
	-Isrc/amd/vulkan/nir \
	-Isrc/vulkan/runtime \
	-Isrc/vulkan/runtime/bvh \
	-Isrc/vulkan/util \
	-Isrc/compiler \
	-Isrc/compiler/nir \
	-Isrc/compiler/spirv \
	-Isrc/gallium/include \
	-Isrc/mesa \
	-Isrc/mesa/main \
	-Isrc/util \
	-Icmd/psbc \
	-Iinclude/mesa \
	-D_XOPEN_SOURCE=500 \
	-DUTIL_ARCH_LITTLE_ENDIAN=1 \
	-DUTIL_ARCH_BIG_ENDIAN=0 \
	-DHAVE_STRUCT_TIMESPEC=1 \
	-DHAVE_PTHREAD=1

# OpenOrbis toolchain
ifeq ($(strip $(OO_PS4_TOOLCHAIN)),)
$(error "Please set OO_PS4_TOOLCHAIN. export OO_PS4_TOOLCHAIN=<path to OpenOrbis-PS4-Toolchain>")
endif

# Cross-compiler targeting PS4 (FreeBSD x86_64)
ARCHFLAGS=--target=x86_64-pc-freebsd12-elf -fPIC -DORBIS -D__ORBIS__ -D__PS4__ \
          -isysroot $(OO_PS4_TOOLCHAIN) \
          -I$(OO_PS4_TOOLCHAIN)/include \
          -I$(OO_PS4_TOOLCHAIN)/include/c++/v1 \
          -I$(OO_PS4_TOOLCHAIN)/include/orbis \
          -Iopenorbis-compat/include

CC=clang
CXX=clang++
LD=clang++
PYTHON=python3
ifeq ($(shell uname -s),Darwin)
AR := /opt/homebrew/opt/llvm/bin/llvm-ar
else
AR := llvm-ar
endif
CFLAGS=-std=gnu11 -Wall -O2 -g $(ARCHFLAGS) $(SHARED_FLAGS) \
       -Wno-macro-redefined -Wno-typedef-redefinition \
       -Wno-unused-function -Wno-unused-variable \
       -Dalloca=__builtin_alloca \
       -DHAVE_SYSCONF=1 \
       -include strings.h
CXXFLAGS=-std=c++17 -Wall -O2 -g $(ARCHFLAGS) $(SHARED_FLAGS) \
         -Wno-macro-redefined -Wno-typedef-redefinition \
         -Wno-unused-function -Wno-unused-variable \
         -Dalloca=__builtin_alloca \
         -DHAVE_SYSCONF=1 \
         -include strings.h
LDFLAGS=-lm
