# Compiled guest C++ realpath provider calls; a nonterminal sink captures observations.
REALPATH_DIR := $(REPO_ROOT)/src/aux/clang/test/realpath
REALPATH_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/realpath
REALPATH_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
REALPATH_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
REALPATH_CC ?= clang
REALPATH_CXX ?= clang++
REALPATH_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
REALPATH_CXXFLAGS := -isystem "$(REALPATH_CLANG_BUILD)/cxx/include/c++/v1" \
	-isystem "$(REALPATH_CLANG_DIR)/include" $(REALPATH_CFLAGS) \
	-isystem "$(REALPATH_CLANG_BUILD)/guest/include" \
	-isystem "$(REALPATH_CLANG_BUILD)/source/llvm/include" \
	-isystem "$(REALPATH_CLANG_BUILD)/source/clang/include" \
	-isystem "$(REALPATH_CLANG_BUILD)/guest/tools/clang/include" \
	-I "$(REALPATH_CLANG_BUILD)/source/llvm/lib/Support" \
	-std=c++17 -DNDEBUG -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -Wno-unused-parameter -MD -MP

.PHONY: realpath-fixture realpath-build-inputs test-realpath
realpath-fixture: $(REALPATH_BUILD)/probe.wasm
realpath-build-inputs:
	$(MAKE) -C ../aux "$(REALPATH_CLANG_BUILD)/guest/.configured"

$(REALPATH_BUILD)/probe.o: $(REALPATH_DIR)/probe.cpp $(REALPATH_DIR)/realpath.mk $(SDK_HEADERS) $(REALPATH_CLANG_DIR)/clang-waste.patch $(REALPATH_CLANG_DIR)/include/limits.h $(REPO_ROOT)/src/config.h | realpath-build-inputs
	mkdir -p "$(@D)"
	$(REALPATH_CXX) $(REALPATH_CXXFLAGS) -c "$<" -o "$@"

$(REALPATH_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(REALPATH_DIR)/realpath.mk
	mkdir -p "$(@D)"
	$(REALPATH_CC) $(REALPATH_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(REALPATH_BUILD)/probe.wasm: $(REALPATH_BUILD)/probe.o $(REALPATH_BUILD)/crt.o $(REALPATH_CLANG_BUILD)/cxx/lib/libc++.a $(REALPATH_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(REALPATH_DIR)/realpath.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(REALPATH_BUILD)/probe.o" \
		"$(REALPATH_BUILD)/crt.o" "$(REALPATH_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(REALPATH_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-realpath: install-libc aux-test-runner realpath-fixture
	bash "$(REALPATH_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(REALPATH_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(REALPATH_BUILD)/probe.d
