# Actual LLVM blocking lock methods and public process observations; WAST owns assertions.
LOCKS_DIR := $(REPO_ROOT)/src/system-tests/clang/locks
LOCKS_BUILD := $(abspath $(BUILD_DIR))/clang/locks
LOCKS_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
LOCKS_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
LOCKS_CC ?= clang
LOCKS_CXX ?= clang++
LOCKS_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
LOCKS_CXXFLAGS := -isystem "$(LOCKS_CLANG_BUILD)/cxx/include/c++/v1" \
	-isystem "$(LOCKS_CLANG_DIR)/include" $(LOCKS_CFLAGS) \
	-isystem "$(LOCKS_CLANG_BUILD)/guest/include" \
	-isystem "$(LOCKS_CLANG_BUILD)/source/llvm/include" \
	-isystem "$(LOCKS_CLANG_BUILD)/source/clang/include" \
	-isystem "$(LOCKS_CLANG_BUILD)/guest/tools/clang/include" \
	-I "$(LOCKS_CLANG_BUILD)/source/llvm/lib/Support" \
	-std=c++17 -DNDEBUG -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -Wno-unused-parameter -MD -MP

.PHONY: locks-fixture locks-build-inputs test-locks
locks-fixture: $(LOCKS_BUILD)/probe.wasm
locks-build-inputs:
	$(MAKE) -C ../aux "$(LOCKS_CLANG_BUILD)/guest/.configured"

$(LOCKS_BUILD)/probe.o: $(LOCKS_DIR)/probe.cpp $(LOCKS_DIR)/locks.mk $(SDK_HEADERS) $(LOCKS_CLANG_DIR)/clang-waste.patch $(LOCKS_CLANG_DIR)/include/limits.h $(REPO_ROOT)/src/config.h | locks-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

$(LOCKS_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(LOCKS_DIR)/locks.mk
	mkdir -p "$(@D)"
	$(LOCKS_CC) $(LOCKS_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(LOCKS_BUILD)/probe.wasm: $(LOCKS_BUILD)/probe.o $(LOCKS_BUILD)/methods.o $(PLATFORM_BUILD)/category.o $(LOCKS_BUILD)/crt.o $(LOCKS_CLANG_BUILD)/cxx/lib/libc++.a $(LOCKS_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(LOCKS_DIR)/locks.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(LOCKS_BUILD)/probe.o" "$(LOCKS_BUILD)/methods.o" "$(PLATFORM_BUILD)/category.o" \
		"$(LOCKS_BUILD)/crt.o" "$(LOCKS_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(LOCKS_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-locks: install-libc aux-test-runner locks-fixture
	bash "$(LOCKS_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(LOCKS_BUILD)" "$(abspath $(WASM_RUNNER))"

$(LOCKS_BUILD)/methods.cpp: $(LOCKS_DIR)/prepare-locks.py $(LOCKS_CLANG_BUILD)/source/llvm/lib/Support/Unix/Path.inc | locks-build-inputs
	$(PYTHON) "$<" "$(LOCKS_CLANG_BUILD)/source/llvm/lib/Support/Unix/Path.inc" "$@"

$(LOCKS_BUILD)/methods.o: $(LOCKS_BUILD)/methods.cpp $(LOCKS_DIR)/locks.mk $(SDK_HEADERS) | locks-build-inputs
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

-include $(LOCKS_BUILD)/probe.d $(LOCKS_BUILD)/methods.d
