# Compile the real staged LLVM Mutex.h with the guest-generated config.
MUTEX_DIR := $(REPO_ROOT)/src/aux/clang/test/mutex
MUTEX_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/mutex
MUTEX_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
MUTEX_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
MUTEX_CC ?= clang
MUTEX_CXX ?= clang++
MUTEX_FLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"

.PHONY: mutex-fixture mutex-build-inputs test-mutex
mutex-fixture: $(MUTEX_BUILD)/probe.wasm
mutex-build-inputs:
	$(MAKE) -C ../aux "$(MUTEX_CLANG_BUILD)/guest/.configured"

$(MUTEX_BUILD)/probe.o: $(MUTEX_DIR)/probe.cpp $(MUTEX_DIR)/mutex.mk $(SDK_HEADERS) $(MUTEX_CLANG_DIR)/include/machine/endian.h $(wildcard $(MUTEX_CLANG_DIR)/*.cmake) $(MUTEX_CLANG_DIR)/clang.mk $(MUTEX_CLANG_DIR)/clang-waste.patch | mutex-build-inputs
	mkdir -p "$(@D)"
	$(MUTEX_CXX) -isystem "$(MUTEX_CLANG_BUILD)/cxx/include/c++/v1" $(MUTEX_FLAGS) \
		-isystem "$(MUTEX_CLANG_DIR)/include" \
		-isystem "$(MUTEX_CLANG_BUILD)/guest/include" -isystem "$(MUTEX_CLANG_BUILD)/source/llvm/include" \
		-std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -MD -MP -c "$<" -o "$@"

$(MUTEX_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(MUTEX_DIR)/mutex.mk
	mkdir -p "$(@D)"
	$(MUTEX_CC) $(MUTEX_FLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(MUTEX_BUILD)/probe.wasm: $(MUTEX_BUILD)/probe.o $(MUTEX_BUILD)/crt.o $(MUTEX_CLANG_BUILD)/cxx/lib/libc++.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(MUTEX_DIR)/mutex.mk
	$(WASM_LD) --no-entry --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(MUTEX_BUILD)/probe.o" "$(MUTEX_BUILD)/crt.o" "$(MUTEX_CLANG_BUILD)/cxx/lib/libc++.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-mutex: install-libc aux-test-runner mutex-fixture
	bash "$(MUTEX_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(MUTEX_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(MUTEX_BUILD)/probe.d
