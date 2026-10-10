.DEFAULT_GOAL := all
REPO_ROOT := $(abspath ../..)
BUILD_DIR ?= $(REPO_ROOT)/build/system-tests
VFS_ROOT ?= $(REPO_ROOT)/src/vfs
include ../aux/test.mk

PYTHON ?= python3
SDK_TOOLS := $(REPO_ROOT)/src/html-rt/tools
SDK_ROOT := $(REPO_ROOT)/src/vfs
SDK_DIR := $(REPO_ROOT)/src/system-tests/guest-sdk
SYSROOT_DIR ?= $(BUILD_DIR)/guest-sdk/sysroot
SDK_HEADERS := $(shell find "$(SDK_ROOT)/usr/include" "$(SDK_ROOT)/usr/lib/waste/cc/include" -name '*.h')
SYSROOT_INPUTS := $(SDK_DIR)/build-sysroot.py $(REPO_ROOT)/src/aux/libc/waste-crt.c \
	$(SDK_HEADERS) $(SDK_ROOT)/usr/share/waste/sdk.json $(SDK_TOOLS)/guest_sdk.py \
	$(SDK_TOOLS)/guest_providers.py $(SDK_TOOLS)/wasm_signatures.py $(SDK_TOOLS)/sdk-api-policy.json
SDK_CC := $(SYSROOT_DIR)/bin/waste-wasm-clang
SDK_BUILD := $(BUILD_DIR)/guest-sdk
TOOLCHAIN_ROOT := $(REPO_ROOT)/build/engine/toolchain/usr
WASM_LD ?= $(shell command -v wasm-ld 2>/dev/null || printf '%s' "$(TOOLCHAIN_ROOT)/bin/wasm-ld")

.PHONY: sysroot check-sdk crt-fixture
sysroot: $(SYSROOT_DIR)/manifest.json
check-sdk:
	$(PYTHON) "$(SDK_DIR)/check.py"

$(SYSROOT_DIR)/manifest.json: $(SYSROOT_INPUTS)
	$(PYTHON) "$(SDK_DIR)/build-sysroot.py" --repo-root "$(REPO_ROOT)" --output "$(abspath $(SYSROOT_DIR))"

$(SDK_BUILD)/waste-crt.o: $(SYSROOT_DIR)/manifest.json $(REPO_ROOT)/src/aux/libc/waste-crt.c
	mkdir -p "$(@D)"
	$(SDK_CC) -c $(REPO_ROOT)/src/aux/libc/waste-crt.c -o "$@"

$(SDK_BUILD)/startup.o: $(SYSROOT_DIR)/manifest.json $(SDK_DIR)/startup.c
	mkdir -p "$(@D)"
	$(SDK_CC) -c $(SDK_DIR)/startup.c -o "$@"

$(SDK_BUILD)/startup.wasm: $(SDK_BUILD)/waste-crt.o $(SDK_BUILD)/startup.o
	env LD_LIBRARY_PATH="$(TOOLCHAIN_ROOT)/lib" $(WASM_LD) --no-entry --gc-sections \
		--export=_start --export-memory --initial-memory=33554432 \
		--max-memory=268435456 $^ -o "$@"

$(SDK_BUILD)/startup.wat: $(SDK_BUILD)/startup.wasm
	wasm-dis "$<" -o "$@"

$(SDK_BUILD)/startup.audit.json: $(SDK_BUILD)/startup.wasm
	$(PYTHON) "$(SDK_TOOLS)/audit-wasm-imports.py" "$<" \
		--allow env:exit --allow env:write --allow waste_kernel:startup_v1 --json "$@"

crt-fixture: $(SDK_BUILD)/startup.audit.json $(SDK_BUILD)/startup.wat
	$(PYTHON) -c 'import json; p=json.load(open("$(SDK_BUILD)/startup.audit.json")); assert p["ok"] and not p["unknown"] and not p["asyncifySymbols"]'

.PHONY: all test test-libc install-libc
all: test
test: test-libc
install-libc:
	$(MAKE) -C ../aux install-libc VFS_ROOT="$(VFS_ROOT)"

test-libc: install-libc aux-test-runner
	bash "$(REPO_ROOT)/src/aux/run-wast-tests.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" system-libc \
		"$(REPO_ROOT)/src/system-tests/libc" "$(abspath $(BUILD_DIR))/libc" "$(abspath $(WASM_RUNNER))"
