# Compiled guest C++ hard-link provider calls; a nonterminal sink captures observations.
HARDLINK_DIR := $(REPO_ROOT)/src/system-tests/clang/hardlink
HARDLINK_BUILD := $(abspath $(BUILD_DIR))/clang/hardlink
HARDLINK_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
HARDLINK_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
HARDLINK_CC ?= clang
HARDLINK_CXX ?= clang++
HARDLINK_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
HARDLINK_CXXFLAGS := -isystem "$(HARDLINK_CLANG_BUILD)/cxx/include/c++/v1" \
	-isystem "$(HARDLINK_CLANG_DIR)/include" $(HARDLINK_CFLAGS) \
	-isystem "$(HARDLINK_CLANG_BUILD)/guest/include" \
	-isystem "$(HARDLINK_CLANG_BUILD)/source/llvm/include" \
	-isystem "$(HARDLINK_CLANG_BUILD)/source/clang/include" \
	-isystem "$(HARDLINK_CLANG_BUILD)/guest/tools/clang/include" \
	-I "$(HARDLINK_CLANG_BUILD)/source/llvm/lib/Support" \
	-std=c++17 -DNDEBUG -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -Wno-unused-parameter -MD -MP

.PHONY: hardlink-fixture hardlink-build-inputs test-hardlink
hardlink-fixture: $(HARDLINK_BUILD)/probe.wasm
hardlink-build-inputs:
	$(MAKE) -C ../aux "$(HARDLINK_CLANG_BUILD)/guest/.configured"

$(HARDLINK_BUILD)/probe.o: $(HARDLINK_DIR)/probe.cpp $(HARDLINK_DIR)/hardlink.mk $(SDK_HEADERS) $(HARDLINK_CLANG_DIR)/clang-waste.patch $(HARDLINK_CLANG_DIR)/include/limits.h $(REPO_ROOT)/src/config.h | hardlink-build-inputs
	mkdir -p "$(@D)"
	$(HARDLINK_CXX) $(HARDLINK_CXXFLAGS) -c "$<" -o "$@"

$(HARDLINK_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(HARDLINK_DIR)/hardlink.mk
	mkdir -p "$(@D)"
	$(HARDLINK_CC) $(HARDLINK_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(HARDLINK_BUILD)/probe.wasm: $(HARDLINK_BUILD)/probe.o $(HARDLINK_BUILD)/crt.o $(HARDLINK_CLANG_BUILD)/cxx/lib/libc++.a $(HARDLINK_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(HARDLINK_DIR)/hardlink.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(HARDLINK_BUILD)/probe.o" \
		"$(HARDLINK_BUILD)/crt.o" "$(HARDLINK_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(HARDLINK_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-hardlink: install-libc aux-test-runner hardlink-fixture
	bash "$(HARDLINK_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(HARDLINK_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(HARDLINK_BUILD)/probe.d

