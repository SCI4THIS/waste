ifeq ($(origin CC), default)
CC := clang
endif
CC ?= clang
PYTHON ?= python3
CFLAGS ?= -O2
AUX_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(AUX_DIR)/../..)
TOOLCHAIN_ROOT := $(REPO_ROOT)/build/engine/toolchain/usr
WASM_LD ?= $(shell command -v wasm-ld 2>/dev/null || printf '%s' "$(TOOLCHAIN_ROOT)/bin/wasm-ld")
ifeq ($(WASM_LD),$(TOOLCHAIN_ROOT)/bin/wasm-ld)
export LD_LIBRARY_PATH := $(TOOLCHAIN_ROOT)/lib$(if $(LD_LIBRARY_PATH),:$(LD_LIBRARY_PATH))
endif
BUILD_DIR ?= $(REPO_ROOT)/build/aux
SDK_ROOT := $(REPO_ROOT)/src/vfs
VFS_ROOT ?= $(SDK_ROOT)
VFS_TOOL := $(REPO_ROOT)/src/html-rt/tools/vfs.py
LIBC_REVIEW := $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
CRT_SOURCE := $(REPO_ROOT)/src/aux/libc/waste-crt.c
CRT_OBJECT := $(BUILD_DIR)/waste-crt.o
SDK_HEADERS := $(shell find "$(SDK_ROOT)/usr/include" "$(SDK_ROOT)/usr/lib/waste/cc/include" -type f -name '*.h')
WASM_CFLAGS := --target=wasm32 -std=c11 -ffreestanding -fno-builtin \
	-DWASTE_WASM -DNDEBUG -fPIC -fno-stack-protector \
	-fdata-sections -ffunction-sections -nostdinc -nostdlib \
	-isystem "$(SDK_ROOT)/usr/include" \
	-isystem "$(SDK_ROOT)/usr/lib/waste/cc/include"
WASM_LDFLAGS := --no-entry --import-memory --import-table \
	--export=_start --export=main --export=__heap_base --export=__data_end \
	--experimental-pic --unresolved-symbols=import-dynamic \
	--global-base=327680 --table-base=1024

ROGUE_DIR := $(AUX_DIR)/rogue
ROGUE_BUILD := $(BUILD_DIR)/rogue
ROGUE_UPSTREAM := $(REPO_ROOT)/submodules/rogue
ROGUE_SOURCES := armor.c chase.c command.c daemon.c daemons.c \
	extern.c fight.c init.c io.c list.c mach_dep.c main.c mdport.c misc.c \
	monsters.c move.c new_level.c options.c pack.c passages.c potions.c \
	rings.c rip.c rooms.c save.c scrolls.c state.c sticks.c things.c vers.c \
	weapons.c wizard.c xcrypt.c
ROGUE_INPUTS := $(addprefix $(ROGUE_UPSTREAM)/,$(ROGUE_SOURCES) rogue.h extern.h score.h)
ROGUE_PATCHES := $(ROGUE_DIR)/platform.patch $(ROGUE_DIR)/debug-dump.patch $(ROGUE_DIR)/rogue-waste.patch
ROGUE_STAMP := $(ROGUE_BUILD)/src/.prepared
ROGUE_OBJECTS := $(addprefix $(ROGUE_BUILD)/objects/,$(ROGUE_SOURCES:.c=.o))
ROGUE_CFLAGS := -DHAVE_CONFIG_H -DWASTE_ROGUE_LEGACY_ABORT \
	-include "$(ROGUE_DIR)/bool-fix.h" -include "$(ROGUE_DIR)/imports.h" \
	-I"$(ROGUE_BUILD)/src" -Wno-unused-parameter -Wno-sign-compare \
	-Wno-implicit-function-declaration -Wno-int-conversion -Wno-dangling-else \
	-Wno-sometimes-uninitialized -Wno-parentheses

.DELETE_ON_ERROR:
.PHONY: all upload download ldd rogue install-upload install-download install-ldd install-rogue
all: upload download ldd rogue

upload: $(BUILD_DIR)/upload/upload.wasm
download: $(BUILD_DIR)/download/download.wasm
ldd: $(BUILD_DIR)/ldd/ldd.wasm
rogue: $(ROGUE_BUILD)/rogue.wasm

install-upload: upload
install-download: download
install-ldd: ldd
install-rogue: rogue

# Keep publication explicit and serialize concurrent installs.
# Python is used only for the existing VFS audit/installation machinery.
install-upload install-download install-ldd install-rogue:
	@set -eu; \
		utility="$(patsubst install-%,%,$@)"; \
		artifact="$(BUILD_DIR)/$$utility/$$utility.wasm"; \
		$(PYTHON) "$(LIBC_REVIEW)" \
			--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$$artifact"; \
		if [ "$$utility" = rogue ]; then \
			$(PYTHON) "$(LIBC_REVIEW)" --namespace libncurses \
				--provider "$(VFS_ROOT)/lib/libncurses.so.wasm" --check-imports "$$artifact"; \
		fi; \
		flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(VFS_TOOL)" install \
			--root "$(VFS_ROOT)" --component "$$utility" --source "$$artifact"

$(CRT_OBJECT): $(CRT_SOURCE) $(AUX_DIR)/Makefile
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) -DWASTE_SHARED_LIBC_IMPORTS \
		-c "$<" -o "$@"

$(BUILD_DIR)/upload/upload.o: $(AUX_DIR)/upload/upload.c $(AUX_DIR)/libc-imports.h $(AUX_DIR)/Makefile $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) \
		-include "$(AUX_DIR)/libc-imports.h" -c "$<" -o "$@"

$(BUILD_DIR)/download/download.o: $(AUX_DIR)/download/download.c $(AUX_DIR)/libc-imports.h $(AUX_DIR)/Makefile $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) \
		-include "$(AUX_DIR)/libc-imports.h" -c "$<" -o "$@"

$(BUILD_DIR)/ldd/ldd.o: $(AUX_DIR)/ldd/ldd.c $(AUX_DIR)/libc-imports.h $(AUX_DIR)/Makefile $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) \
		-include "$(AUX_DIR)/libc-imports.h" -c "$<" -o "$@"

$(BUILD_DIR)/upload/upload.wasm: $(CRT_OBJECT) $(BUILD_DIR)/upload/upload.o
	$(WASM_LD) $(LDFLAGS) $(WASM_LDFLAGS) -o "$@" $(foreach object,$^,"$(object)")

$(BUILD_DIR)/download/download.wasm: $(CRT_OBJECT) $(BUILD_DIR)/download/download.o
	$(WASM_LD) $(LDFLAGS) $(WASM_LDFLAGS) -o "$@" $(foreach object,$^,"$(object)")

$(BUILD_DIR)/ldd/ldd.wasm: $(CRT_OBJECT) $(BUILD_DIR)/ldd/ldd.o
	$(WASM_LD) $(LDFLAGS) $(WASM_LDFLAGS) -o "$@" $(foreach object,$^,"$(object)")

# Upstream checkouts remain read-only. Re-copy originals before applying any
# changed patch, and publish the stamp only after every patch succeeds.
$(ROGUE_STAMP): $(ROGUE_INPUTS) $(ROGUE_PATCHES) $(ROGUE_DIR)/config.h $(AUX_DIR)/Makefile
	mkdir -p "$(@D)"
	rm -f "$@"
	cp $(foreach source,$(ROGUE_INPUTS),"$(source)") "$(@D)/"
	chmod u+w "$(@D)/"*.c "$(@D)/"*.h
	cp "$(ROGUE_DIR)/config.h" "$(@D)/config.h"
	$(foreach patch,$(ROGUE_PATCHES),patch --batch --forward --fuzz=0 -d "$(@D)" -p1 -i "$(patch)" &&) touch "$@"

# Objects depend on the preparation stamp rather than submodule source paths:
# all compilation and dependency output stays in build/aux/rogue.
$(ROGUE_OBJECTS): $(ROGUE_BUILD)/objects/%.o: $(ROGUE_STAMP) $(ROGUE_DIR)/bool-fix.h $(ROGUE_DIR)/imports.h $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) $(ROGUE_CFLAGS) \
		-MMD -MP -c "$(ROGUE_BUILD)/src/$*.c" -o "$@"

$(ROGUE_BUILD)/rogue.wasm: $(ROGUE_OBJECTS) $(CRT_OBJECT)
	$(WASM_LD) $(LDFLAGS) $(WASM_LDFLAGS) -o "$@" $(foreach object,$^,"$(object)")

-include $(ROGUE_OBJECTS:.o=.d)

include $(AUX_DIR)/libc/libc.mk
include $(AUX_DIR)/test.mk
include $(AUX_DIR)/coreutils/coreutils.mk
include $(AUX_DIR)/libncurses/libncurses.mk
include $(AUX_DIR)/bash/bash.mk
