include config.mak

ifeq ($(PLATFORM), orbis)
CFLAGS += -DOPENGNM_ORBIS
endif

# opengnm source files
OPENGNM_SRCS = \
	src/drawcommandbuffer.c \
	src/rendertarget.c \
	src/depthrendertarget.c \
	src/texture.c \
	src/shader.c \
	src/dataformat.c \
	src/commandbuffer.c \
	src/error.c \
	src/helpers.c

ifeq ($(PLATFORM), orbis)
OPENGNM_SRCS += \
	src/driver_orbis.c \
	src/platform_orbis.c
else
OPENGNM_SRCS += \
	src/driver_generic.c \
	src/platform_generic.c
endif

# GpuAddr (AMD PAL-derived surface computation)
OPENGNM_SRCS += \
	src/gpuaddr/surface.c \
	src/gpuaddr/tilemodes.c \
	src/gpuaddr/tiler.c \
	src/gpuaddr/decompress.c \
	src/gpuaddr/surfgen.c \
	src/gpuaddr/error.c

# GCN assembler (internal)
OPENGNM_SRCS += \
	src/gcn/analyzer.c \
	src/gcn/assembler.c \
	src/gcn/decoder.c \
	src/gcn/error.c \
	src/gcn/format.c \
	src/gcn/types.c

# PM4 encoding
OPENGNM_SRCS += \
	src/pm4/format.c \
	src/pm4/decoder.c \
	src/pm4/error.c \
	src/pm4/types.c

# Source list is already clean (no comment lines in the list above)
OPENGNM_SRCS := $(strip $(OPENGNM_SRCS))
OPENGNM_OBJS = $(OPENGNM_SRCS:.c=.o)

# === Library ===
LIB_NAME = libopengnm
LIB_STATIC = $(LIB_NAME).a
LIB_SHARED = $(LIB_NAME)$(LIBEXT).$(LIBVER)

.PHONY: all lib tools tests link-smoke hardware-smoke hardware-smoke-pkg clean install

all: lib

lib:
ifneq ($(OPENGNM_SRCS),)
	@set -e; for src in $(OPENGNM_SRCS); do \
		obj=$$(echo $$src | sed 's/\.c$$/.o/') ; \
		dir=$$(dirname $$obj) ; \
		mkdir -p $$dir ; \
		echo "CC  $$src" ; \
		$(CC) $(CFLAGS) -c $$src -o $$obj ; \
	done
	rm -f $(LIB_STATIC)
	$(AR) rcs $(LIB_STATIC) $(OPENGNM_OBJS)
else
	@echo "opengnm: No source files configured yet (Phase 1 — headers only)"
	@echo "opengnm: Headers are in include/ — use 'make install' to install them"
endif

# === Install ===
install: install-headers

install-headers:
	install -d $(DESTDIR)$(INCDIR)/opengnm
	install -m 644 include/*.h $(DESTDIR)$(INCDIR)/opengnm/
	cp -R include/compat include/gnm include/pm4 $(DESTDIR)$(INCDIR)/opengnm/

install-lib: lib
	install -d $(DESTDIR)$(LIBDIR)
	install -m 644 $(LIB_STATIC) $(DESTDIR)$(LIBDIR)/

# === Tests ===
TEST_SRCS = \
	tests/test_main.c \
	tests/test_surface.c \
	tests/test_drawcmd.c \
	tests/test_validate.c \
	tests/test_api.c \
	tests/test_compat.c \
	tests/test_helpers.c \
	tests/test_pm4.c

TEST_BIN = opengnm_tests
TEST_OBJS = $(TEST_SRCS:.c=.o)

tests: lib
	@echo "CC  tests"
	@set -e; for src in $(TEST_SRCS); do \
		obj=$$(echo $$src | sed 's/\.c$$/.o/') ; \
		dir=$$(dirname $$obj) ; \
		mkdir -p $$dir ; \
		$(CC) $(CFLAGS) -I./include -I./src -I./tests -c $$src -o $$obj ; \
	done
	$(CC) $(LDFLAGS) $(TEST_OBJS) -o $(TEST_BIN) $(LIB_STATIC)
	./$(TEST_BIN)

# === Orbis link smoke test ===
LINK_SMOKE_SRC = tests/link_smoke.c
LINK_SMOKE_OBJ = tests/link_smoke.o
LINK_SMOKE_ELF = opengnm_link_smoke.elf

HW_SMOKE_SRC = tests/hardware_smoke.c
HW_SMOKE_OBJ = tests/hardware_smoke.o
HW_SMOKE_ELF = opengnm_hw_smoke.elf
HW_SMOKE_EXE = opengnm_hw_smoke
HW_SMOKE_OELF = $(HW_SMOKE_EXE).oelf

HW_SMOKE_TITLE = opengnm Hardware Smoke
HW_SMOKE_VERSION = 1.00
HW_SMOKE_TITLE_ID = OGNM00001
HW_SMOKE_CONTENT_ID = IV0000-OGNM00001_00-OPENGNMHWSMOKE00
HW_SMOKE_PKG = $(HW_SMOKE_CONTENT_ID).pkg

CREATE_FSELF ?= $(TOOLCHAIN)/bin/linux/create-fself
CREATE_GP4 ?= $(TOOLCHAIN)/bin/linux/create-gp4
PKGTOOL ?= $(TOOLCHAIN)/bin/linux/PkgTool.Core
PKG_ASSET_DIR ?= ../freegnm-examples/videoout-linear
RUNTIME_MODULES = sce_module/libc.prx sce_module/libSceFios2.prx
RUNTIME_MODULE_DIR ?= $(TOOLCHAIN)/bin/data/modules
PKG_FILES = eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png $(RUNTIME_MODULES)

$(HW_SMOKE_OBJ): $(HW_SMOKE_SRC)
ifeq ($(PLATFORM),orbis)
	@echo "CC  $(HW_SMOKE_SRC)"
	$(CC) $(CFLAGS) -D_BSD_SOURCE -I./tests -c $(HW_SMOKE_SRC) -o $(HW_SMOKE_OBJ)
else
	@echo "$(HW_SMOKE_OBJ) requires PLATFORM=orbis"
	@exit 1
endif

link-smoke: lib
ifeq ($(PLATFORM),orbis)
	@echo "CC  $(LINK_SMOKE_SRC)"
	$(CC) $(CFLAGS) -I./tests -c $(LINK_SMOKE_SRC) -o $(LINK_SMOKE_OBJ)
	@echo "LD  $(LINK_SMOKE_ELF)"
	$(LD) -m elf_x86_64 -e main -L$(TOOLCHAIN)/lib -L. $(LINK_SMOKE_OBJ) $(LIB_STATIC) -lc -lkernel -lSceGnmDriver -lSceVideoOut -o $(LINK_SMOKE_ELF)
else
	@echo "link-smoke requires PLATFORM=orbis"
	@exit 1
endif

hardware-smoke: lib $(HW_SMOKE_OBJ)
ifeq ($(PLATFORM),orbis)
	@echo "LD  $(HW_SMOKE_ELF)"
	$(LD) -m elf_x86_64 -e main -L$(TOOLCHAIN)/lib -L. $(HW_SMOKE_OBJ) $(LIB_STATIC) -lc -lkernel -lSceGnmDriver -lSceVideoOut -o $(HW_SMOKE_ELF)
else
	@echo "hardware-smoke requires PLATFORM=orbis"
	@exit 1
endif

hardware-smoke-pkg: $(HW_SMOKE_PKG)

$(HW_SMOKE_EXE): lib $(HW_SMOKE_OBJ)
ifeq ($(PLATFORM),orbis)
	@echo "LD  $(HW_SMOKE_EXE)"
	$(LD) -o $(HW_SMOKE_EXE) $(HW_SMOKE_OBJ) $(LIB_STATIC) \
		-m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr \
		-L$(TOOLCHAIN)/lib -lc -lkernel -lSceGnmDriver -lSceVideoOut \
		$(TOOLCHAIN)/lib/crt1.o $(TOOLCHAIN)/lib/crti.o $(TOOLCHAIN)/lib/crtn.o
else
	@echo "$(HW_SMOKE_EXE) requires PLATFORM=orbis"
	@exit 1
endif

eboot.bin: $(HW_SMOKE_EXE)
	$(CREATE_FSELF) -in=$(HW_SMOKE_EXE) -out=$(HW_SMOKE_OELF) -eboot=eboot.bin --paid 0x3800000000000011

sce_module:
	mkdir -p $@

sce_module/libc.prx: $(RUNTIME_MODULE_DIR)/libc.prx | sce_module
	cp $< $@

sce_module/libSceFios2.prx: $(RUNTIME_MODULE_DIR)/libSceFios2.prx | sce_module
	cp $< $@

sce_sys/about:
	mkdir -p $@

sce_sys/icon0.png: $(PKG_ASSET_DIR)/sce_sys/icon0.png | sce_sys/about
	cp $< $@

sce_sys/about/right.sprx: $(PKG_ASSET_DIR)/sce_sys/about/right.sprx | sce_sys/about
	cp $< $@

sce_sys/param.sfo: Makefile | sce_sys/about
	$(PKGTOOL) sfo_new $@
	$(PKGTOOL) sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(PKGTOOL) sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(HW_SMOKE_VERSION)'
	$(PKGTOOL) sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(PKGTOOL) sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(HW_SMOKE_CONTENT_ID)'
	$(PKGTOOL) sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(PKGTOOL) sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(HW_SMOKE_TITLE)'
	$(PKGTOOL) sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(HW_SMOKE_TITLE_ID)'
	$(PKGTOOL) sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(HW_SMOKE_VERSION)'

pkg.gp4: $(PKG_FILES)
	$(CREATE_GP4) -out=$@ -content-id=$(HW_SMOKE_CONTENT_ID) -files "$(PKG_FILES)"

$(HW_SMOKE_PKG): pkg.gp4
	$(PKGTOOL) pkg_build $< .
	$(PKGTOOL) pkg_validate --verbose $(HW_SMOKE_PKG)

# === Clean ===
clean:
	find . -name '*.o' -path '*/src/*' -delete
	find . -name '*.o' -path '*/tests/*' -delete
	rm -f $(LIB_STATIC) $(LIB_SHARED) $(TEST_BIN) $(LINK_SMOKE_ELF) $(HW_SMOKE_ELF)
	rm -f $(HW_SMOKE_EXE) $(HW_SMOKE_OELF) $(HW_SMOKE_PKG) eboot.bin pkg.gp4
	rm -rf sce_module sce_sys
