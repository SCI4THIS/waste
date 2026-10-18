# Actual LLVM disabled-thread latch, TaskGroup and scalar algorithms.
PARALLEL_DIR := $(REPO_ROOT)/src/aux/clang/test/parallel
PARALLEL_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/parallel
PARALLEL_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
PARALLEL_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
PARALLEL_CC ?= clang
PARALLEL_CXX ?= clang++
PARALLEL_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
PARALLEL_CXXFLAGS := -isystem "$(PARALLEL_CLANG_BUILD)/cxx/include/c++/v1" \
	$(PARALLEL_CFLAGS) -isystem "$(PARALLEL_CLANG_DIR)/include" \
	-isystem "$(PARALLEL_CLANG_BUILD)/guest/include" \
	-isystem "$(PARALLEL_CLANG_BUILD)/source/llvm/include" \
	-isystem "$(PARALLEL_CLANG_BUILD)/source/clang/include" \
	-isystem "$(PARALLEL_CLANG_BUILD)/guest/tools/clang/include" \
	-std=c++17 -DNDEBUG -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -MD -MP

.PHONY: parallel-fixture parallel-build-inputs parallel-native-source test-parallel
parallel-fixture: $(PARALLEL_BUILD)/probe.wasm
parallel-build-inputs:
	$(MAKE) -C ../aux "$(PARALLEL_CLANG_BUILD)/guest/.configured"

$(PARALLEL_BUILD)/probe.o: $(PARALLEL_DIR)/probe.cpp $(PARALLEL_DIR)/parallel.mk $(SDK_HEADERS) $(PARALLEL_CLANG_DIR)/clang-waste.patch | parallel-build-inputs
	mkdir -p "$(@D)"
	$(PARALLEL_CXX) $(PARALLEL_CXXFLAGS) -c "$<" -o "$@"

# Compile actual scalar task/loop code; no task, wait or provider is mocked.
$(PARALLEL_BUILD)/parallel.o: $(PARALLEL_DIR)/parallel.mk $(SDK_HEADERS) $(PARALLEL_CLANG_DIR)/clang-waste.patch | parallel-build-inputs
	mkdir -p "$(@D)"
	$(PARALLEL_CXX) $(PARALLEL_CXXFLAGS) -Wno-unused-parameter \
		-c "$(PARALLEL_CLANG_BUILD)/source/llvm/lib/Support/Parallel.cpp" -o "$@"

$(PARALLEL_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(PARALLEL_DIR)/parallel.mk
	mkdir -p "$(@D)"
	$(PARALLEL_CC) $(PARALLEL_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(PARALLEL_BUILD)/probe.wasm: $(PARALLEL_BUILD)/probe.o $(PARALLEL_BUILD)/parallel.o $(PARALLEL_BUILD)/crt.o $(PARALLEL_CLANG_BUILD)/cxx/lib/libc++.a $(PARALLEL_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(PARALLEL_DIR)/parallel.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(PARALLEL_BUILD)/probe.o" "$(PARALLEL_BUILD)/parallel.o" \
		"$(PARALLEL_BUILD)/crt.o" "$(PARALLEL_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(PARALLEL_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-parallel: install-libc aux-test-runner parallel-fixture
	bash "$(PARALLEL_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(PARALLEL_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(PARALLEL_BUILD)/probe.d $(PARALLEL_BUILD)/parallel.d

parallel-native-source: | parallel-build-inputs
	mkdir -p "$(PARALLEL_BUILD)/native"
	$(PARALLEL_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
		-fno-exceptions -fno-rtti -isystem "$(PARALLEL_CLANG_BUILD)/native/include" \
		-isystem "$(PARALLEL_CLANG_BUILD)/source/llvm/include" \
		-I "$(PARALLEL_CLANG_BUILD)/source/llvm/lib/Support" \
		-c "$(PARALLEL_DIR)/native.cpp" -o "$(PARALLEL_BUILD)/native/Parallel.o"
	$(PARALLEL_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
		-DWASTE_TEST_THREADED_BRANCH -fno-exceptions -fno-rtti \
		-isystem "$(PARALLEL_CLANG_BUILD)/native/include" \
		-isystem "$(PARALLEL_CLANG_BUILD)/source/llvm/include" \
		-I "$(PARALLEL_CLANG_BUILD)/source/llvm/lib/Support" \
		-c "$(PARALLEL_DIR)/native.cpp" -o "$(PARALLEL_BUILD)/native/Parallel-threaded.o"
