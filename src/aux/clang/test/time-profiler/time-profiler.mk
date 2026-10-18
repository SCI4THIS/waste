# Actual LLVM profiler storage and archive-backed public lifecycle.
TIME_PROFILER_DIR := $(REPO_ROOT)/src/aux/clang/test/time-profiler
TIME_PROFILER_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/time-profiler
TIME_PROFILER_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
TIME_PROFILER_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
TIME_PROFILER_SUPPORT := $(TIME_PROFILER_CLANG_BUILD)/guest/lib/libLLVMSupport.a
TIME_PROFILER_CC ?= clang
TIME_PROFILER_CXX ?= clang++
TIME_PROFILER_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
TIME_PROFILER_CXXFLAGS := -isystem "$(TIME_PROFILER_CLANG_BUILD)/cxx/include/c++/v1" \
	$(TIME_PROFILER_CFLAGS) -isystem "$(TIME_PROFILER_CLANG_DIR)/include" \
	-isystem "$(TIME_PROFILER_CLANG_BUILD)/guest/include" \
	-isystem "$(TIME_PROFILER_CLANG_BUILD)/source/llvm/include" \
	-isystem "$(TIME_PROFILER_CLANG_BUILD)/source/clang/include" \
	-isystem "$(TIME_PROFILER_CLANG_BUILD)/guest/tools/clang/include" \
	-I "$(TIME_PROFILER_CLANG_BUILD)/source/llvm/lib/Support" \
	-std=c++17 -DNDEBUG -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -MD -MP

.PHONY: time-profiler-fixture time-profiler-build-inputs time-profiler-native-source test-time-profiler
time-profiler-fixture: $(TIME_PROFILER_BUILD)/probe.wasm $(TIME_PROFILER_BUILD)/lifecycle.wasm
time-profiler-build-inputs:
	$(MAKE) -C ../aux "$(TIME_PROFILER_CLANG_BUILD)/guest/.configured"

$(TIME_PROFILER_BUILD)/probe.o: $(TIME_PROFILER_DIR)/probe.cpp $(TIME_PROFILER_DIR)/time-profiler.mk $(SDK_HEADERS) $(TIME_PROFILER_CLANG_DIR)/clang-waste.patch | time-profiler-build-inputs
	mkdir -p "$(@D)"
	$(TIME_PROFILER_CXX) $(TIME_PROFILER_CXXFLAGS) -c "$<" -o "$@"

$(TIME_PROFILER_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(TIME_PROFILER_DIR)/time-profiler.mk
	mkdir -p "$(@D)"
	$(TIME_PROFILER_CC) $(TIME_PROFILER_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(TIME_PROFILER_BUILD)/probe.wasm: $(TIME_PROFILER_BUILD)/probe.o $(TIME_PROFILER_BUILD)/crt.o $(TIME_PROFILER_CLANG_BUILD)/cxx/lib/libc++.a $(TIME_PROFILER_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(TIME_PROFILER_DIR)/time-profiler.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(TIME_PROFILER_BUILD)/probe.o" \
		"$(TIME_PROFILER_BUILD)/crt.o" "$(TIME_PROFILER_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(TIME_PROFILER_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

TIME_PROFILER_SESSION_RUNNER ?= $(REPO_ROOT)/build/cli-rt/private/guest-session$(if $(findstring sanitize,$(WASM_RUNNER)),-sanitize,)

test-time-profiler: install-libc aux-test-runner time-profiler-fixture time-profiler-session-runner
	bash "$(TIME_PROFILER_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(TIME_PROFILER_BUILD)" "$(abspath $(WASM_RUNNER))"
	$(PYTHON) "$(TIME_PROFILER_DIR)/lifecycle-native.py" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(TIME_PROFILER_BUILD)" "$(TIME_PROFILER_SESSION_RUNNER)"

.PHONY: test-time-profiler-lifecycle time-profiler-session-runner
time-profiler-session-runner:
	$(MAKE) -C ../cli-rt guest-session guest-session-sanitize

test-time-profiler-lifecycle: install-libc $(TIME_PROFILER_BUILD)/lifecycle.wasm time-profiler-session-runner
	$(PYTHON) "$(TIME_PROFILER_DIR)/lifecycle-native.py" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(TIME_PROFILER_BUILD)" "$(TIME_PROFILER_SESSION_RUNNER)"

$(TIME_PROFILER_BUILD)/lifecycle.o: $(TIME_PROFILER_DIR)/lifecycle.cpp $(TIME_PROFILER_DIR)/time-profiler.mk $(SDK_HEADERS) | program-execution-build-inputs
	mkdir -p "$(@D)"
	$(TIME_PROFILER_CXX) $(TIME_PROFILER_CXXFLAGS) -c "$<" -o "$@"

$(TIME_PROFILER_BUILD)/lifecycle.wasm: $(TIME_PROFILER_BUILD)/lifecycle.o $(TIME_PROFILER_BUILD)/crt.o $(TIME_PROFILER_SUPPORT) $(TIME_PROFILER_CLANG_BUILD)/guest/lib/libLLVMDemangle.a $(TIME_PROFILER_CLANG_BUILD)/cxx/lib/libc++.a $(TIME_PROFILER_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main --export=__stack_pointer \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		--wrap=malloc --wrap=free "$(TIME_PROFILER_BUILD)/lifecycle.o" "$(TIME_PROFILER_BUILD)/crt.o" \
		"$(TIME_PROFILER_SUPPORT)" "$(TIME_PROFILER_CLANG_BUILD)/guest/lib/libLLVMDemangle.a" \
		"$(TIME_PROFILER_CLANG_BUILD)/cxx/lib/libc++.a" "$(TIME_PROFILER_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

-include $(TIME_PROFILER_BUILD)/probe.d $(TIME_PROFILER_BUILD)/lifecycle.d

time-profiler-native-source: | time-profiler-build-inputs
	mkdir -p "$(TIME_PROFILER_BUILD)/native"
	$(TIME_PROFILER_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
		-fno-exceptions -fno-rtti -isystem "$(TIME_PROFILER_CLANG_BUILD)/native/include" \
		-isystem "$(TIME_PROFILER_CLANG_BUILD)/source/llvm/include" \
		-I "$(TIME_PROFILER_CLANG_BUILD)/source/llvm/lib/Support" \
		-c "$(TIME_PROFILER_DIR)/native.cpp" -o "$(TIME_PROFILER_BUILD)/native/TimeProfiler.o"
	$(TIME_PROFILER_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
		-DWASTE_TEST_THREADED_BRANCH -fno-exceptions -fno-rtti \
		-isystem "$(TIME_PROFILER_CLANG_BUILD)/native/include" \
		-isystem "$(TIME_PROFILER_CLANG_BUILD)/source/llvm/include" \
		-I "$(TIME_PROFILER_CLANG_BUILD)/source/llvm/lib/Support" \
		-c "$(TIME_PROFILER_DIR)/native.cpp" -o "$(TIME_PROFILER_BUILD)/native/TimeProfiler-threaded.o"
