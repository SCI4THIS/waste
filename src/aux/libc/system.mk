# SDK infrastructure, system libc tests, devices and abort fixtures. Previously
# lived in src/system-tests/Makefile. BUILD_DIR for system tests remains
# $(REPO_ROOT)/build/system-tests so existing output paths and browser JSON
# source references do not change.

SYSTEM_TESTS_BUILD ?= $(REPO_ROOT)/build/system-tests
SDK_TOOLS := $(REPO_ROOT)/src/html-rt/tools
SDK_DIR := $(REPO_ROOT)/src/aux/libc/test/sdk
SYSROOT_DIR ?= $(SYSTEM_TESTS_BUILD)/guest-sdk/sysroot
SDK_CC := $(SYSROOT_DIR)/bin/waste-wasm-clang
SDK_BUILD := $(SYSTEM_TESTS_BUILD)/guest-sdk
SYSROOT_INPUTS := $(SDK_DIR)/build-sysroot.py $(REPO_ROOT)/src/aux/libc/waste-crt.c \
	$(SDK_HEADERS) $(SDK_ROOT)/usr/share/waste/sdk.json $(SDK_TOOLS)/guest_sdk.py \
	$(SDK_TOOLS)/guest_providers.py $(SDK_TOOLS)/wasm_signatures.py $(SDK_TOOLS)/sdk-api-policy.json

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

.PHONY: test-libc-system
test-libc-system: install-libc aux-test-runner
	bash "$(REPO_ROOT)/src/aux/run-wast-tests.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" system-libc \
		"$(REPO_ROOT)/src/vfs/root/test/system/libc" "$(SYSTEM_TESTS_BUILD)/libc" "$(abspath $(WASM_RUNNER))"

ABORT_DIR := $(REPO_ROOT)/src/engine/test/abort
ABORT_BUILD := $(SYSTEM_TESTS_BUILD)/abort
ABORT_CFLAGS := --target=wasm32 -std=c11 -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
.PHONY: test-abort abort-fixture
abort-fixture: $(ABORT_BUILD)/probe.wasm
$(ABORT_BUILD)/probe.o: $(ABORT_DIR)/probe.c $(SDK_HEADERS) $(REPO_ROOT)/src/aux/libc-imports.h $(LIBC_DIR)/system.mk
	mkdir -p "$(@D)"
	clang $(ABORT_CFLAGS) -c "$<" -o "$@"
$(ABORT_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(LIBC_DIR)/system.mk
	mkdir -p "$(@D)"
	clang $(ABORT_CFLAGS) -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"
$(ABORT_BUILD)/probe.wasm: $(ABORT_BUILD)/probe.o $(ABORT_BUILD)/crt.o
	$(WASM_LD) --no-entry --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		$^ -o "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"
test-abort: install-libc aux-test-runner abort-fixture
	bash "$(ABORT_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(ABORT_BUILD)" "$(abspath $(WASM_RUNNER))"

.PHONY: test-devices
test-devices: install-libc aux-test-runner
	bash "$(REPO_ROOT)/src/aux/run-wast-tests.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" system-devices \
		"$(REPO_ROOT)/src/vfs/root/test/system/devices" "$(SYSTEM_TESTS_BUILD)/devices" "$(abspath $(WASM_RUNNER))"

# Binary handler is a cli-rt test; its recipe uses aux-test-runner, so include
# it with the other aux-run system tests.
include $(REPO_ROOT)/src/cli-rt/test/binary-handler/binary-handler.mk
