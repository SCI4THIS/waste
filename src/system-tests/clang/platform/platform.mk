# C++ SDK/provider boundary and actual optional LLVM filesystem queries.
PLATFORM_DIR := $(REPO_ROOT)/src/system-tests/clang/platform
PLATFORM_BUILD := $(abspath $(BUILD_DIR))/clang/platform
PLATFORM_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
PLATFORM_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
PLATFORM_CC ?= clang
PLATFORM_CXX ?= clang++
PLATFORM_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
PLATFORM_CXXFLAGS := -isystem "$(PLATFORM_CLANG_BUILD)/cxx/include/c++/v1" \
	$(PLATFORM_CFLAGS) -isystem "$(PLATFORM_CLANG_DIR)/include" \
	-isystem "$(PLATFORM_CLANG_BUILD)/guest/include" \
	-isystem "$(PLATFORM_CLANG_BUILD)/source/llvm/include" \
	-isystem "$(PLATFORM_CLANG_BUILD)/source/clang/include" \
	-isystem "$(PLATFORM_CLANG_BUILD)/guest/tools/clang/include" \
	-I "$(PLATFORM_CLANG_BUILD)/source/llvm/lib/Support" \
	-std=c++17 -DNDEBUG -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -Wno-unused-parameter -MD -MP

PLATFORM_OBJECTS := $(PLATFORM_BUILD)/queries.o $(PLATFORM_BUILD)/category.o

.PHONY: platform-fixture platform-build-inputs test-platform
platform-fixture: $(PLATFORM_BUILD)/probe.wasm $(PLATFORM_BUILD)/library.so.wasm
platform-build-inputs:
	$(MAKE) -C ../aux "$(PLATFORM_CLANG_BUILD)/guest/.configured"

$(PLATFORM_BUILD)/probe.o: $(PLATFORM_DIR)/probe.cpp $(PLATFORM_DIR)/platform.mk $(SDK_HEADERS) $(PLATFORM_CLANG_DIR)/clang-waste.patch | platform-build-inputs
	mkdir -p "$(@D)"
	$(PLATFORM_CXX) $(PLATFORM_CXXFLAGS) -c "$<" -o "$@"

$(PLATFORM_BUILD)/queries.cpp: $(PLATFORM_DIR)/prepare-queries.py $(PLATFORM_CLANG_BUILD)/source/llvm/lib/Support/Unix/Path.inc | platform-build-inputs
	$(PYTHON) "$<" "$(PLATFORM_CLANG_BUILD)/source/llvm/lib/Support/Unix/Path.inc" "$@"

$(PLATFORM_BUILD)/queries.o: $(PLATFORM_BUILD)/queries.cpp $(PLATFORM_DIR)/platform.mk $(SDK_HEADERS) | platform-build-inputs
	$(PLATFORM_CXX) $(PLATFORM_CXXFLAGS) -c "$<" -o "$@"

# Real error-category source with ordinary static registration and shared exit.
$(PLATFORM_BUILD)/category.o: $(PLATFORM_CLANG_BUILD)/source/libcxx/src/system_error.cpp $(PLATFORM_DIR)/platform.mk $(SDK_HEADERS) | platform-build-inputs
	$(PLATFORM_CXX) $(PLATFORM_CXXFLAGS) -D_LIBCPP_BUILDING_LIBRARY \
		-std=c++26 -fvisibility=hidden -fsized-deallocation \
		-Wno-error=cpp -Wno-error=pragma-clang-attribute \
		-c "$<" -o "$@"

$(PLATFORM_BUILD)/library.o: $(PLATFORM_DIR)/library.c $(PLATFORM_DIR)/platform.mk
	mkdir -p "$(@D)"
	$(PLATFORM_CC) $(PLATFORM_CFLAGS) -ffunction-sections -fdata-sections -c "$<" -o "$@"

$(PLATFORM_BUILD)/library.so.wasm: $(PLATFORM_BUILD)/library.o $(PLATFORM_DIR)/platform.mk
	$(WASM_LD) --shared --no-entry --experimental-pic --import-memory --import-table --export=platform_add "$<" -o "$@"

$(PLATFORM_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(PLATFORM_DIR)/platform.mk
	mkdir -p "$(@D)"
	$(PLATFORM_CC) $(PLATFORM_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(PLATFORM_BUILD)/probe.wasm: $(PLATFORM_BUILD)/probe.o $(PLATFORM_OBJECTS) $(PLATFORM_BUILD)/crt.o $(PLATFORM_CLANG_BUILD)/cxx/lib/libc++.a $(PLATFORM_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(PLATFORM_DIR)/platform.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(PLATFORM_BUILD)/probe.o" $(PLATFORM_OBJECTS) \
		"$(PLATFORM_BUILD)/crt.o" "$(PLATFORM_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(PLATFORM_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-platform: install-libc aux-test-runner platform-fixture
	bash "$(PLATFORM_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(PLATFORM_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(PLATFORM_BUILD)/probe.d $(PLATFORM_OBJECTS:.o=.d)

