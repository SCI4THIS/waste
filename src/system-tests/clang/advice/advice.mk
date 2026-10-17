# Public advisory calls and unchanged LLVM mapping methods; WAST owns assertions.
ADVICE_DIR := $(REPO_ROOT)/src/system-tests/clang/advice
ADVICE_BUILD := $(abspath $(BUILD_DIR))/clang/advice
ADVICE_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
ADVICE_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
ADVICE_CC ?= clang
ADVICE_CXX ?= clang++
ADVICE_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
ADVICE_CXXFLAGS := -isystem "$(ADVICE_CLANG_BUILD)/cxx/include/c++/v1" \
	-isystem "$(ADVICE_CLANG_DIR)/include" $(ADVICE_CFLAGS) \
	-isystem "$(ADVICE_CLANG_BUILD)/guest/include" \
	-isystem "$(ADVICE_CLANG_BUILD)/source/llvm/include" \
	-isystem "$(ADVICE_CLANG_BUILD)/source/clang/include" \
	-isystem "$(ADVICE_CLANG_BUILD)/guest/tools/clang/include" \
	-I "$(ADVICE_CLANG_BUILD)/source/llvm/lib/Support" \
	-std=c++17 -DNDEBUG -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -Wno-unused-parameter -MD -MP

ADVICE_OBJECTS := $(ADVICE_BUILD)/mapping.o $(ADVICE_BUILD)/ranges.o $(PLATFORM_BUILD)/category.o

.PHONY: advice-fixture advice-build-inputs test-advice
advice-fixture: $(ADVICE_BUILD)/probe.wasm
advice-build-inputs:
	$(MAKE) -C ../aux "$(ADVICE_CLANG_BUILD)/guest/.configured"

$(ADVICE_BUILD)/probe.o: $(ADVICE_DIR)/probe.cpp $(ADVICE_DIR)/advice.mk $(SDK_HEADERS) $(ADVICE_CLANG_DIR)/clang-waste.patch $(ADVICE_CLANG_DIR)/include/limits.h $(REPO_ROOT)/src/config.h | advice-build-inputs
	mkdir -p "$(@D)"
	$(ADVICE_CXX) $(ADVICE_CXXFLAGS) -c "$<" -o "$@"

$(ADVICE_BUILD)/mapping.cpp: $(ADVICE_DIR)/prepare-mapping.py $(ADVICE_CLANG_BUILD)/source/llvm/lib/Support/Unix/Path.inc $(ADVICE_CLANG_BUILD)/source/llvm/lib/Support/Path.cpp | advice-build-inputs
	$(PYTHON) "$<" "$(ADVICE_CLANG_BUILD)/source/llvm/lib/Support/Unix/Path.inc" "$(ADVICE_CLANG_BUILD)/source/llvm/lib/Support/Path.cpp" "$@"

$(ADVICE_BUILD)/mapping.o: $(ADVICE_BUILD)/mapping.cpp $(ADVICE_DIR)/advice.mk $(SDK_HEADERS) | advice-build-inputs
	$(ADVICE_CXX) $(ADVICE_CXXFLAGS) -c "$<" -o "$@"

$(ADVICE_BUILD)/ranges.o: $(ADVICE_DIR)/ranges.cpp $(ADVICE_DIR)/advice.mk $(SDK_HEADERS) | advice-build-inputs
	$(ADVICE_CXX) $(ADVICE_CXXFLAGS) -c "$<" -o "$@"

$(ADVICE_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(ADVICE_DIR)/advice.mk
	mkdir -p "$(@D)"
	$(ADVICE_CC) $(ADVICE_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(ADVICE_BUILD)/probe.wasm: $(ADVICE_BUILD)/probe.o $(ADVICE_OBJECTS) $(ADVICE_BUILD)/crt.o $(ADVICE_CLANG_BUILD)/cxx/lib/libc++.a $(ADVICE_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(ADVICE_DIR)/prepare-imports.py $(ADVICE_DIR)/advice.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(ADVICE_BUILD)/probe.o" $(ADVICE_OBJECTS) \
		"$(ADVICE_BUILD)/crt.o" "$(ADVICE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(ADVICE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(ADVICE_DIR)/prepare-imports.py" "$@.raw" "$(VFS_ROOT)/lib/libc.so.wasm" "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-advice: install-libc aux-test-runner advice-fixture
	bash "$(ADVICE_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(ADVICE_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(ADVICE_BUILD)/probe.d $(ADVICE_BUILD)/mapping.d $(ADVICE_BUILD)/ranges.d
