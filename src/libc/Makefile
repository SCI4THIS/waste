ifeq ($(origin CC), default)
CC := clang
endif
CC ?= clang
PYTHON ?= python3
CFLAGS ?= -O2
LIBC_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(LIBC_DIR)/../..)
BUILD_DIR ?= $(REPO_ROOT)/build/libc-shared
SDK_ROOT := $(REPO_ROOT)/src/vfs
VFS_ROOT ?= $(SDK_ROOT)
VFS_TOOL := $(REPO_ROOT)/src/html-rt/tools/vfs.py
TOOLCHAIN_ROOT := $(REPO_ROOT)/build/engine/toolchain/usr
WASM_LD ?= $(shell command -v wasm-ld 2>/dev/null || printf '%s' "$(TOOLCHAIN_ROOT)/bin/wasm-ld")
ifeq ($(WASM_LD),$(TOOLCHAIN_ROOT)/bin/wasm-ld)
export LD_LIBRARY_PATH := $(TOOLCHAIN_ROOT)/lib$(if $(LD_LIBRARY_PATH),:$(LD_LIBRARY_PATH))
endif

include $(LIBC_DIR)/sources.mk
LIBC_SOURCES := allocator.c $(LIBC_C_SOURCES)
LIBC_OBJECTS := $(addprefix $(BUILD_DIR)/objects/,$(LIBC_SOURCES:.c=.o))
LIBC_OUTPUT := $(BUILD_DIR)/libc.so.wasm
SDK_HEADERS := $(shell find "$(SDK_ROOT)/usr/include" "$(SDK_ROOT)/usr/lib/waste/cc/include" -type f -name '*.h')
WASM_CFLAGS := --target=wasm32 -std=c11 -ffreestanding -fno-builtin \
	-DWASTE_WASM -DNDEBUG -fPIC -fvisibility=default \
	-DWASTE_POSIX_IO -DWASTE_SHARED_LIBC -fno-stack-protector \
	-fdata-sections -ffunction-sections -nostdinc -nostdlib \
	-isystem "$(SDK_ROOT)/usr/include" \
	-isystem "$(SDK_ROOT)/usr/lib/waste/cc/include"
WASM_LDFLAGS := --shared --import-memory --import-table \
	--export-all --allow-undefined --no-entry

.DELETE_ON_ERROR:
.PHONY: all install
all: $(LIBC_OUTPUT)

$(BUILD_DIR)/objects/%.o: $(LIBC_DIR)/%.c $(LIBC_DIR)/include/helper.h $(LIBC_DIR)/Makefile $(LIBC_DIR)/sources.mk $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(LIBC_OUTPUT): $(LIBC_OBJECTS) $(LIBC_DIR)/Makefile $(LIBC_DIR)/sources.mk
	$(WASM_LD) $(LDFLAGS) $(WASM_LDFLAGS) -o "$@" $(foreach object,$(LIBC_OBJECTS),"$(object)")

# Installation is explicit; packaging reads only the installed VFS snapshot.
# Python is used only for the existing VFS audit/installation machinery.
install: all
	flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(VFS_TOOL)" install \
		--root "$(VFS_ROOT)" --component libc --source "$(LIBC_OUTPUT)"

-include $(LIBC_OBJECTS:.o=.d)
