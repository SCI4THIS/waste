# Guest Mustache numeric interpolation and real C++ string ownership.
MUSTACHE_DIR := $(REPO_ROOT)/src/aux/clang/test/mustache
MUSTACHE_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/mustache
MUSTACHE_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
MUSTACHE_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
MUSTACHE_CC ?= clang
MUSTACHE_CXX ?= clang++
MUSTACHE_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
MUSTACHE_CXXFLAGS := -isystem "$(MUSTACHE_CLANG_BUILD)/cxx/include/c++/v1" \
	$(MUSTACHE_CFLAGS) -isystem "$(MUSTACHE_CLANG_DIR)/include" \
	-isystem "$(MUSTACHE_CLANG_BUILD)/guest/include" \
	-isystem "$(MUSTACHE_CLANG_BUILD)/source/llvm/include" \
	-isystem "$(MUSTACHE_CLANG_BUILD)/source/clang/include" \
	-isystem "$(MUSTACHE_CLANG_BUILD)/guest/tools/clang/include" \
	-std=c++17 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -MD -MP

.PHONY: mustache-fixture mustache-build-inputs mustache-native-source test-mustache
mustache-fixture: $(MUSTACHE_BUILD)/probe.wasm $(MUSTACHE_BUILD)/reference.bin
mustache-build-inputs:
	$(MAKE) -C ../aux "$(MUSTACHE_CLANG_BUILD)/guest/.configured"

$(MUSTACHE_BUILD)/probe.o: $(MUSTACHE_DIR)/probe.cpp $(MUSTACHE_DIR)/values.inc $(MUSTACHE_CLANG_DIR)/include/llvm/Support/WasteMustache.h $(MUSTACHE_DIR)/mustache.mk $(SDK_HEADERS) $(MUSTACHE_CLANG_DIR)/clang-waste.patch | mustache-build-inputs
	mkdir -p "$(@D)"
	$(MUSTACHE_CXX) $(MUSTACHE_CXXFLAGS) -c "$<" -o "$@"

$(MUSTACHE_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(MUSTACHE_DIR)/mustache.mk
	mkdir -p "$(@D)"
	$(MUSTACHE_CC) $(MUSTACHE_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(MUSTACHE_BUILD)/probe.wasm: $(MUSTACHE_BUILD)/probe.o $(MUSTACHE_BUILD)/crt.o $(MUSTACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MUSTACHE_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(MUSTACHE_DIR)/mustache.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(MUSTACHE_BUILD)/probe.o" \
		"$(MUSTACHE_BUILD)/crt.o" "$(MUSTACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MUSTACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-mustache: install-libc aux-test-runner mustache-fixture
	bash "$(MUSTACHE_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(MUSTACHE_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(MUSTACHE_BUILD)/probe.d

# Compile the actual native implementation with its original ostringstream path.
mustache-native-source: | mustache-build-inputs
	mkdir -p "$(MUSTACHE_BUILD)/native"
	$(MUSTACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
		-fno-exceptions -fno-rtti -isystem "$(MUSTACHE_CLANG_BUILD)/native/include" \
		-isystem "$(MUSTACHE_CLANG_BUILD)/source/llvm/include" \
		-c "$(MUSTACHE_CLANG_BUILD)/source/llvm/lib/Support/Mustache.cpp" \
		-o "$(MUSTACHE_BUILD)/native/Mustache.o"

$(MUSTACHE_BUILD)/reference: $(MUSTACHE_DIR)/probe.cpp $(MUSTACHE_DIR)/values.inc $(MUSTACHE_DIR)/mustache.mk
	mkdir -p "$(@D)"
	$(MUSTACHE_CXX) -std=c++17 -Wall -Wextra -Werror -DMUSTACHE_HOST_REFERENCE "$<" -o "$@"

$(MUSTACHE_BUILD)/reference.bin: $(MUSTACHE_BUILD)/reference
	"$<" > "$@"
