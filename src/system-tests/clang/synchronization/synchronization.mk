# LLVM synchronization declarations and real generic allocator ownership.
SYNCHRONIZATION_DIR := $(REPO_ROOT)/src/system-tests/clang/synchronization
SYNCHRONIZATION_BUILD := $(abspath $(BUILD_DIR))/clang/synchronization
SYNCHRONIZATION_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
SYNCHRONIZATION_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
SYNCHRONIZATION_CC ?= clang
SYNCHRONIZATION_CXX ?= clang++
SYNCHRONIZATION_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
SYNCHRONIZATION_CXXFLAGS := -isystem "$(SYNCHRONIZATION_CLANG_BUILD)/cxx/include/c++/v1" \
	$(SYNCHRONIZATION_CFLAGS) -isystem "$(SYNCHRONIZATION_CLANG_DIR)/include" \
	-isystem "$(SYNCHRONIZATION_CLANG_BUILD)/guest/include" \
	-isystem "$(SYNCHRONIZATION_CLANG_BUILD)/source/llvm/include" \
	-isystem "$(SYNCHRONIZATION_CLANG_BUILD)/source/clang/include" \
	-isystem "$(SYNCHRONIZATION_CLANG_BUILD)/guest/tools/clang/include" \
	-std=c++17 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -MD -MP

.PHONY: synchronization-fixture synchronization-build-inputs synchronization-native-headers test-synchronization
synchronization-fixture: $(SYNCHRONIZATION_BUILD)/probe.wasm $(SYNCHRONIZATION_BUILD)/headers.o
synchronization-build-inputs:
	$(MAKE) -C ../aux "$(SYNCHRONIZATION_CLANG_BUILD)/guest/.configured"

synchronization-native-headers: | synchronization-build-inputs
	$(SYNCHRONIZATION_CXX) -std=c++17 -Wall -Wextra -Werror -fsyntax-only \
		-isystem "$(SYNCHRONIZATION_CLANG_BUILD)/native/include" \
		-isystem "$(SYNCHRONIZATION_CLANG_BUILD)/source/llvm/include" "$(SYNCHRONIZATION_DIR)/native.cpp"
	$(SYNCHRONIZATION_CXX) -std=c++17 -Wall -Wextra -Werror -fsyntax-only -DWASTE_TEST_THREADED_BRANCH \
		-isystem "$(SYNCHRONIZATION_CLANG_BUILD)/native/include" \
		-isystem "$(SYNCHRONIZATION_CLANG_BUILD)/source/llvm/include" "$(SYNCHRONIZATION_DIR)/native.cpp"

$(SYNCHRONIZATION_BUILD)/probe.o: $(SYNCHRONIZATION_DIR)/probe.cpp $(SYNCHRONIZATION_DIR)/synchronization.mk $(SDK_HEADERS) $(SYNCHRONIZATION_CLANG_DIR)/clang-waste.patch | synchronization-build-inputs
	mkdir -p "$(@D)"
	$(SYNCHRONIZATION_CXX) $(SYNCHRONIZATION_CXXFLAGS) -c "$<" -o "$@"

$(SYNCHRONIZATION_BUILD)/headers.o: $(SYNCHRONIZATION_DIR)/headers.cpp $(SYNCHRONIZATION_DIR)/synchronization.mk $(SDK_HEADERS) $(SYNCHRONIZATION_CLANG_DIR)/clang-waste.patch | synchronization-build-inputs
	mkdir -p "$(@D)"
	$(SYNCHRONIZATION_CXX) $(SYNCHRONIZATION_CXXFLAGS) -c "$<" -o "$@"

# Retain LLVM's actual atomic process-unique ID implementation. Unused printer
# sections are discarded at link time, without replacing their implementations.
$(SYNCHRONIZATION_BUILD)/allocator.o: $(SYNCHRONIZATION_DIR)/synchronization.mk $(SDK_HEADERS) $(SYNCHRONIZATION_CLANG_DIR)/clang-waste.patch | synchronization-build-inputs
	mkdir -p "$(@D)"
	$(SYNCHRONIZATION_CXX) $(SYNCHRONIZATION_CXXFLAGS) \
		-c "$(SYNCHRONIZATION_CLANG_BUILD)/source/llvm/lib/Support/Allocator.cpp" -o "$@"

$(SYNCHRONIZATION_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(SYNCHRONIZATION_DIR)/synchronization.mk
	mkdir -p "$(@D)"
	$(SYNCHRONIZATION_CC) $(SYNCHRONIZATION_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(SYNCHRONIZATION_BUILD)/probe.wasm: $(SYNCHRONIZATION_BUILD)/probe.o $(SYNCHRONIZATION_BUILD)/allocator.o $(SYNCHRONIZATION_BUILD)/crt.o $(SYNCHRONIZATION_CLANG_BUILD)/cxx/lib/libc++.a $(SYNCHRONIZATION_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(SYNCHRONIZATION_DIR)/synchronization.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(SYNCHRONIZATION_BUILD)/probe.o" "$(SYNCHRONIZATION_BUILD)/allocator.o" \
		"$(SYNCHRONIZATION_BUILD)/crt.o" "$(SYNCHRONIZATION_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(SYNCHRONIZATION_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-synchronization: install-libc aux-test-runner synchronization-fixture
	bash "$(SYNCHRONIZATION_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(SYNCHRONIZATION_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(SYNCHRONIZATION_BUILD)/probe.d $(SYNCHRONIZATION_BUILD)/allocator.d $(SYNCHRONIZATION_BUILD)/headers.d
