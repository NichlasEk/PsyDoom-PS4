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


CC=clang
CXX=clang++
LD=clang++
AR=llvm-ar
PYTHON=python3
CFLAGS=-std=gnu11 -O2 $(SHARED_FLAGS) -D_GNU_SOURCE -DHAVE_SYSCONF=1
CXXFLAGS=-std=c++17 -O2 $(SHARED_FLAGS) -D_GNU_SOURCE -DHAVE_SYSCONF=1
CFLAGS+=-DBLAKE3_NO_SSE2 -DBLAKE3_NO_SSE41 -DBLAKE3_NO_AVX2 -DBLAKE3_NO_AVX512 -DBLAKE3_USE_NEON=0
LDFLAGS=-lm -lpthread
