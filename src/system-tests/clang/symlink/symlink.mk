# Compiled guest C++ symlink provider calls; a nonterminal sink captures observations.
SYMLINK_DIR := $(REPO_ROOT)/src/system-tests/clang/symlink
SYMLINK_BUILD := $(abspath $(BUILD_DIR))/clang/symlink
SYMLINK_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
SYMLINK_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
SYMLINK_CC ?= clang
SYMLINK_CXX ?= clang++
SYMLINK_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
SYMLINK_CXXFLAGS := -isystem "$(SYMLINK_CLANG_BUILD)/cxx/include/c++/v1" \
	-isystem "$(SYMLINK_CLANG_DIR)/include" $(SYMLINK_CFLAGS) \
	-isystem "$(SYMLINK_CLANG_BUILD)/guest/include" \
	-isystem "$(SYMLINK_CLANG_BUILD)/source/llvm/include" \
	-isystem "$(SYMLINK_CLANG_BUILD)/source/clang/include" \
	-isystem "$(SYMLINK_CLANG_BUILD)/guest/tools/clang/include" \
	-I "$(SYMLINK_CLANG_BUILD)/source/llvm/lib/Support" \
	-std=c++17 -DNDEBUG -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -Wno-unused-parameter -MD -MP

.PHONY: symlink-fixture symlink-build-inputs test-symlink
symlink-fixture: $(SYMLINK_BUILD)/probe.wasm
symlink-build-inputs:
	$(MAKE) -C ../aux "$(SYMLINK_CLANG_BUILD)/guest/.configured"

$(SYMLINK_BUILD)/probe.o: $(SYMLINK_DIR)/probe.cpp $(SYMLINK_DIR)/symlink.mk $(SDK_HEADERS) $(SYMLINK_CLANG_DIR)/clang-waste.patch $(SYMLINK_CLANG_DIR)/include/limits.h $(REPO_ROOT)/src/config.h | symlink-build-inputs
	mkdir -p "$(@D)"
	$(SYMLINK_CXX) $(SYMLINK_CXXFLAGS) -c "$<" -o "$@"

$(SYMLINK_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(SYMLINK_DIR)/symlink.mk
	mkdir -p "$(@D)"
	$(SYMLINK_CC) $(SYMLINK_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(SYMLINK_BUILD)/probe.wasm: $(SYMLINK_BUILD)/probe.o $(SYMLINK_BUILD)/crt.o $(SYMLINK_CLANG_BUILD)/cxx/lib/libc++.a $(SYMLINK_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(SYMLINK_DIR)/symlink.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(SYMLINK_BUILD)/probe.o" \
		"$(SYMLINK_BUILD)/crt.o" "$(SYMLINK_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(SYMLINK_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-symlink: install-libc aux-test-runner symlink-fixture
	bash "$(SYMLINK_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(SYMLINK_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(SYMLINK_BUILD)/probe.d

