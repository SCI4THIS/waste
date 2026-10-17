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

# All compiled C++ fixtures share the aux-owned generated SDK headers.
$(REPO_ROOT)/build/aux/clang/cxx/.headers: $(SDK_HEADERS) $(wildcard $(REPO_ROOT)/src/aux/clang/*.cmake) $(REPO_ROOT)/src/aux/clang/clang.mk
	$(MAKE) -C ../aux clang-sdk-check

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

.PHONY: test-clang install-clang
install-clang:
	$(MAKE) -C ../aux install-clang VFS_ROOT="$(VFS_ROOT)"

test-clang: install-clang aux-test-runner
	bash "$(REPO_ROOT)/src/aux/run-wast-tests.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" system-clang \
		"$(REPO_ROOT)/src/system-tests/clang" "$(abspath $(BUILD_DIR))/clang" "$(abspath $(WASM_RUNNER))" \
		"$(REPO_ROOT)/src/system-tests/clang/setup.sh"

ABORT_DIR := $(REPO_ROOT)/src/system-tests/abort
ABORT_BUILD := $(abspath $(BUILD_DIR))/abort
ABORT_CFLAGS := --target=wasm32 -std=c11 -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
.PHONY: test-abort abort-fixture
abort-fixture: $(ABORT_BUILD)/probe.wasm
$(ABORT_BUILD)/probe.o: $(ABORT_DIR)/probe.c $(SDK_HEADERS) $(REPO_ROOT)/src/aux/libc-imports.h Makefile
	mkdir -p "$(@D)"
	clang $(ABORT_CFLAGS) -c "$<" -o "$@"
$(ABORT_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) Makefile
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

include clang/stdio/stdio.mk

include clang/classification/classification.mk

include clang/endian/endian.mk

include clang/wide/wide.mk

include clang/wide-format/wide-format.mk

include clang/filesystem/filesystem.mk

include clang/mutex/mutex.mk

include clang/logb/logb.mk

include clang/synchronization/synchronization.mk

include clang/thread-pool/thread-pool.mk

include clang/checkpoint/checkpoint.mk
include clang/backoff/backoff.mk
include clang/lock-owner/lock-owner.mk
include clang/managed-static/managed-static.mk
include clang/mustache/mustache.mk
include clang/parallel/parallel.mk
include clang/time-profiler/time-profiler.mk
include clang/module-cache/module-cache.mk
include clang/module-tasks/module-tasks.mk
include clang/lld-diagnostics/lld-diagnostics.mk
include clang/lld-filesystem/lld-filesystem.mk
include clang/lld-link/lld-link.mk
include clang/lto-errors/lto-errors.mk
include clang/target-host/target-host.mk
include clang/dwarf-verifier/dwarf-verifier.mk
include clang/dwarf-context/dwarf-context.mk
include clang/gsym/gsym.mk
include clang/cfg-format/cfg-format.mk
include clang/inlining/inlining.mk
include clang/folding/folding.mk
include clang/code-layout/code-layout.mk
include clang/assignment-format/assignment-format.mk
include clang/shadow-callbacks/shadow-callbacks.mk
include clang/memprof-format/memprof-format.mk
include clang/raw-stream/raw-stream.mk
include clang/platform/platform.mk
include clang/executable-path/executable-path.mk

include clang/symlink/symlink.mk

include clang/hardlink/hardlink.mk

include clang/accounts/accounts.mk

include clang/advice/advice.mk

include clang/realpath/realpath.mk

include clang/locks/locks.mk

include clang/timed-locks/timed-locks.mk

include clang/process/process.mk

include clang/exec-wait/exec-wait.mk

include clang/program-wait/program-wait.mk

include clang/argument-limits/argument-limits.mk

include clang/session/session.mk

include clang/signals/signals.mk
include clang/watchdog/watchdog.mk

include clang/program-execution/program-execution.mk
include clang/child-deadline/child-deadline.mk
include clang/finite-wait/finite-wait.mk
include clang/crash-recovery/crash-recovery.mk
include clang/startup/startup.mk
include clang/shutdown/shutdown.mk

.PHONY: test-devices
test-devices: install-libc aux-test-runner
	bash "$(REPO_ROOT)/src/aux/run-wast-tests.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" system-devices \
		"$(REPO_ROOT)/src/system-tests/devices" "$(abspath $(BUILD_DIR))/devices" "$(abspath $(WASM_RUNNER))"

include clang/timestamp/timestamp.mk
include clang/coverage-format/coverage-format.mk
include clang/dylib-uuid/dylib-uuid.mk
include clang/issue-hash/issue-hash.mk

include clang/thread-safety/thread-safety.mk

include clang/unsafe-buffer/unsafe-buffer.mk

include clang/seed-file/seed-file.mk

include clang/layout-override/layout-override.mk

include clang/spirv-name/spirv-name.mk
include clang/compiler-imports/compiler-imports.mk
include cli-runtime/binary-handler/binary-handler.mk
