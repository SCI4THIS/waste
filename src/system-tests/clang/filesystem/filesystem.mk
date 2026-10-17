# Compile the real staged LLVM FileSystem.h with the guest-generated config.
FILESYSTEM_DIR := $(REPO_ROOT)/src/system-tests/clang/filesystem
FILESYSTEM_BUILD := $(abspath $(BUILD_DIR))/clang/filesystem
FILESYSTEM_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
FILESYSTEM_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
FILESYSTEM_CC ?= clang
FILESYSTEM_CXX ?= clang++
FILESYSTEM_SESSION_RUNNER ?= $(REPO_ROOT)/build/cli-rt/private/guest-session
FILESYSTEM_FLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"

.PHONY: filesystem-fixture filesystem-build-inputs test-filesystem
filesystem-fixture: $(FILESYSTEM_BUILD)/probe.wasm
filesystem-build-inputs:
	$(MAKE) -C ../aux "$(FILESYSTEM_CLANG_BUILD)/guest/.configured"

$(FILESYSTEM_BUILD)/probe.o: $(FILESYSTEM_DIR)/probe.cpp $(FILESYSTEM_DIR)/filesystem.mk $(SDK_HEADERS) $(FILESYSTEM_CLANG_DIR)/include/machine/endian.h $(wildcard $(FILESYSTEM_CLANG_DIR)/*.cmake) $(FILESYSTEM_CLANG_DIR)/clang.mk $(FILESYSTEM_CLANG_DIR)/clang-waste.patch | filesystem-build-inputs
	mkdir -p "$(@D)"
	$(FILESYSTEM_CXX) -isystem "$(FILESYSTEM_CLANG_BUILD)/cxx/include/c++/v1" $(FILESYSTEM_FLAGS) \
		-isystem "$(FILESYSTEM_CLANG_DIR)/include" \
		-isystem "$(FILESYSTEM_CLANG_BUILD)/guest/include" -isystem "$(FILESYSTEM_CLANG_BUILD)/source/llvm/include" \
		-std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -MD -MP -c "$<" -o "$@"

$(FILESYSTEM_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(FILESYSTEM_DIR)/filesystem.mk
	mkdir -p "$(@D)"
	$(FILESYSTEM_CC) $(FILESYSTEM_FLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(FILESYSTEM_BUILD)/probe.wasm: $(FILESYSTEM_BUILD)/probe.o $(FILESYSTEM_BUILD)/crt.o $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(FILESYSTEM_DIR)/filesystem.mk
	$(WASM_LD) --no-entry --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(FILESYSTEM_BUILD)/probe.o" "$(FILESYSTEM_BUILD)/crt.o" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-filesystem: install-libc aux-test-runner filesystem-fixture
	bash "$(FILESYSTEM_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(FILESYSTEM_BUILD)" "$(abspath $(WASM_RUNNER))"

.PHONY: filesystem-private-runner test-filesystem-controlled
filesystem-private-runner:
	$(MAKE) -C ../cli-rt guest-session $(if $(filter %guest-session-sanitize,$(FILESYSTEM_SESSION_RUNNER)),guest-session-sanitize)

test-filesystem-controlled: install-libc filesystem-private-runner filesystem-fixture
	bash "$(FILESYSTEM_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(FILESYSTEM_BUILD)" "$(abspath $(FILESYSTEM_SESSION_RUNNER))" 4294967313123456789

-include $(FILESYSTEM_BUILD)/probe.d
