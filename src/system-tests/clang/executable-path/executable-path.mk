# Actual LLVM executable-path core; a nonterminal sink captures observations.
EXECUTABLE_PATH_DIR := $(REPO_ROOT)/src/system-tests/clang/executable-path
EXECUTABLE_PATH_BUILD := $(abspath $(BUILD_DIR))/clang/executable-path
EXECUTABLE_PATH_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
EXECUTABLE_PATH_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
EXECUTABLE_PATH_CC ?= clang
EXECUTABLE_PATH_CXX ?= clang++
EXECUTABLE_PATH_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
EXECUTABLE_PATH_CXXFLAGS := -isystem "$(EXECUTABLE_PATH_CLANG_BUILD)/cxx/include/c++/v1" \
	-isystem "$(EXECUTABLE_PATH_CLANG_DIR)/include" $(EXECUTABLE_PATH_CFLAGS) \
	-isystem "$(EXECUTABLE_PATH_CLANG_BUILD)/guest/include" \
	-isystem "$(EXECUTABLE_PATH_CLANG_BUILD)/source/llvm/include" \
	-isystem "$(EXECUTABLE_PATH_CLANG_BUILD)/source/clang/include" \
	-isystem "$(EXECUTABLE_PATH_CLANG_BUILD)/guest/tools/clang/include" \
	-I "$(EXECUTABLE_PATH_CLANG_BUILD)/source/llvm/lib/Support" \
	-std=c++17 -DNDEBUG -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -Wno-unused-parameter -MD -MP

EXECUTABLE_PATH_OBJECTS := $(EXECUTABLE_PATH_BUILD)/path.o

.PHONY: executable-path-fixture executable-path-build-inputs test-executable-path
executable-path-fixture: $(EXECUTABLE_PATH_BUILD)/probe.wasm
executable-path-build-inputs:
	$(MAKE) -C ../aux "$(EXECUTABLE_PATH_CLANG_BUILD)/guest/.configured"

$(EXECUTABLE_PATH_BUILD)/probe.o: $(EXECUTABLE_PATH_DIR)/probe.cpp $(EXECUTABLE_PATH_DIR)/executable-path.mk $(SDK_HEADERS) $(EXECUTABLE_PATH_CLANG_DIR)/clang-waste.patch $(EXECUTABLE_PATH_CLANG_DIR)/include/limits.h $(REPO_ROOT)/src/config.h | executable-path-build-inputs
	mkdir -p "$(@D)"
	$(EXECUTABLE_PATH_CXX) $(EXECUTABLE_PATH_CXXFLAGS) -c "$<" -o "$@"

$(EXECUTABLE_PATH_BUILD)/path.cpp: $(EXECUTABLE_PATH_DIR)/prepare-path.py $(EXECUTABLE_PATH_CLANG_BUILD)/source/llvm/lib/Support/Unix/Path.inc | executable-path-build-inputs
	$(PYTHON) "$<" "$(EXECUTABLE_PATH_CLANG_BUILD)/source/llvm/lib/Support/Unix/Path.inc" "$@"

$(EXECUTABLE_PATH_BUILD)/path.o: $(EXECUTABLE_PATH_BUILD)/path.cpp $(EXECUTABLE_PATH_DIR)/executable-path.mk $(SDK_HEADERS) $(EXECUTABLE_PATH_CLANG_DIR)/include/limits.h $(REPO_ROOT)/src/config.h | executable-path-build-inputs
	$(EXECUTABLE_PATH_CXX) $(EXECUTABLE_PATH_CXXFLAGS) -c "$<" -o "$@"

$(EXECUTABLE_PATH_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(EXECUTABLE_PATH_DIR)/executable-path.mk
	mkdir -p "$(@D)"
	$(EXECUTABLE_PATH_CC) $(EXECUTABLE_PATH_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(EXECUTABLE_PATH_BUILD)/probe.wasm: $(EXECUTABLE_PATH_BUILD)/probe.o $(EXECUTABLE_PATH_OBJECTS) $(EXECUTABLE_PATH_BUILD)/crt.o $(EXECUTABLE_PATH_CLANG_BUILD)/cxx/lib/libc++.a $(EXECUTABLE_PATH_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(EXECUTABLE_PATH_DIR)/executable-path.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(EXECUTABLE_PATH_BUILD)/probe.o" $(EXECUTABLE_PATH_OBJECTS) \
		"$(EXECUTABLE_PATH_BUILD)/crt.o" "$(EXECUTABLE_PATH_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(EXECUTABLE_PATH_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-executable-path: install-libc aux-test-runner executable-path-fixture
	bash "$(EXECUTABLE_PATH_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(EXECUTABLE_PATH_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(EXECUTABLE_PATH_BUILD)/probe.d $(EXECUTABLE_PATH_OBJECTS:.o=.d)

