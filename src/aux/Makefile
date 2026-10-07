ifeq ($(origin CC), default)
CC := clang
endif
CC ?= clang
WASM_LD ?= wasm-ld
PYTHON ?= python3
CFLAGS ?= -O2
AUX_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(AUX_DIR)/../..)
BUILD_DIR ?= $(REPO_ROOT)/build/aux
SDK_ROOT := $(REPO_ROOT)/src/vfs
VFS_ROOT ?= $(SDK_ROOT)
VFS_TOOL := $(REPO_ROOT)/src/html-rt/tools/vfs.py
LIBC_REVIEW := $(REPO_ROOT)/src/html-rt/tools/shared_libc.py
CRT_SOURCE := $(REPO_ROOT)/src/libc/waste-crt.c
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

.DELETE_ON_ERROR:
.PHONY: all upload download ldd install-upload install-download install-ldd
all: upload download ldd

upload: $(BUILD_DIR)/upload/upload.wasm
download: $(BUILD_DIR)/download/download.wasm
ldd: $(BUILD_DIR)/ldd/ldd.wasm

install-upload: upload
install-download: download
install-ldd: ldd

# Keep inventory publication explicit and serialize concurrent installs.
# Python is used only for the existing VFS audit/installation machinery.
# ldd retains the former builder's explicit kernel fstat import review.
install-upload install-download install-ldd:
	@set -eu; \
		utility="$(patsubst install-%,%,$@)"; \
		artifact="$(BUILD_DIR)/$$utility/$$utility.wasm"; \
		review_flags=$$($(PYTHON) "$(LIBC_REVIEW)" \
			--provider "$(VFS_ROOT)/lib/libc.so.wasm" --review-imports "$$artifact"); \
		flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(VFS_TOOL)" install \
			--root "$(VFS_ROOT)" --component "$$utility" --source "$$artifact" \
			$$review_flags $(if $(filter install-ldd,$@),--review-import env:fstat:function)

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
