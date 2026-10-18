# Actual LLVM SingleThreadExecutor and guest deferred results.
THREAD_POOL_DIR := $(REPO_ROOT)/src/aux/clang/test/thread-pool
THREAD_POOL_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/thread-pool
THREAD_POOL_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
THREAD_POOL_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
THREAD_POOL_CC ?= clang
THREAD_POOL_CXX ?= clang++
THREAD_POOL_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
THREAD_POOL_CXXFLAGS := -isystem "$(THREAD_POOL_CLANG_BUILD)/cxx/include/c++/v1" \
	$(THREAD_POOL_CFLAGS) -isystem "$(THREAD_POOL_CLANG_DIR)/include" \
	-isystem "$(THREAD_POOL_CLANG_BUILD)/guest/include" \
	-isystem "$(THREAD_POOL_CLANG_BUILD)/source/llvm/include" \
	-isystem "$(THREAD_POOL_CLANG_BUILD)/source/clang/include" \
	-isystem "$(THREAD_POOL_CLANG_BUILD)/guest/tools/clang/include" \
	-std=c++17 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -MD -MP

.PHONY: thread-pool-fixture thread-pool-build-inputs thread-pool-native-headers test-thread-pool
thread-pool-fixture: $(THREAD_POOL_BUILD)/probe.wasm
thread-pool-build-inputs:
	$(MAKE) -C ../aux "$(THREAD_POOL_CLANG_BUILD)/guest/.configured"

thread-pool-native-headers: | thread-pool-build-inputs
	$(THREAD_POOL_CXX) -std=c++17 -Wall -Wextra -Werror -fsyntax-only \
		-isystem "$(THREAD_POOL_CLANG_BUILD)/native/include" \
		-isystem "$(THREAD_POOL_CLANG_BUILD)/source/llvm/include" "$(THREAD_POOL_DIR)/native.cpp"
	$(THREAD_POOL_CXX) -std=c++17 -Wall -Wextra -Werror -fsyntax-only -DWASTE_TEST_THREADED_BRANCH \
		-isystem "$(THREAD_POOL_CLANG_BUILD)/native/include" \
		-isystem "$(THREAD_POOL_CLANG_BUILD)/source/llvm/include" "$(THREAD_POOL_DIR)/native.cpp"

$(THREAD_POOL_BUILD)/probe.o: $(THREAD_POOL_DIR)/probe.cpp $(THREAD_POOL_DIR)/thread-pool.mk $(SDK_HEADERS) $(THREAD_POOL_CLANG_DIR)/clang-waste.patch $(THREAD_POOL_CLANG_DIR)/include/llvm/Support/ThreadPoolFuture.h | thread-pool-build-inputs
	mkdir -p "$(@D)"
	$(THREAD_POOL_CXX) $(THREAD_POOL_CXXFLAGS) -flto -c "$<" -o "$@"

# Link-time optimization removes the unused diagnostic paths from the actual
# ThreadPool/Threading sources. No constructor, scheduler or provider is mocked.
$(THREAD_POOL_BUILD)/thread-pool.o: $(THREAD_POOL_DIR)/thread-pool.mk $(SDK_HEADERS) $(THREAD_POOL_CLANG_DIR)/clang-waste.patch $(THREAD_POOL_CLANG_DIR)/include/llvm/Support/ThreadPoolFuture.h | thread-pool-build-inputs
	mkdir -p "$(@D)"
	$(THREAD_POOL_CXX) $(THREAD_POOL_CXXFLAGS) -Wno-unused-parameter -flto \
		-c "$(THREAD_POOL_CLANG_BUILD)/source/llvm/lib/Support/ThreadPool.cpp" -o "$@"

$(THREAD_POOL_BUILD)/threading.o: $(THREAD_POOL_DIR)/thread-pool.mk $(SDK_HEADERS) $(THREAD_POOL_CLANG_DIR)/clang-waste.patch | thread-pool-build-inputs
	mkdir -p "$(@D)"
	$(THREAD_POOL_CXX) $(THREAD_POOL_CXXFLAGS) -Wno-unused-parameter -flto \
		-c "$(THREAD_POOL_CLANG_BUILD)/source/llvm/lib/Support/Threading.cpp" -o "$@"

$(THREAD_POOL_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(THREAD_POOL_DIR)/thread-pool.mk
	mkdir -p "$(@D)"
	$(THREAD_POOL_CC) $(THREAD_POOL_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(THREAD_POOL_BUILD)/probe.wasm: $(THREAD_POOL_BUILD)/probe.o $(THREAD_POOL_BUILD)/thread-pool.o $(THREAD_POOL_BUILD)/threading.o $(THREAD_POOL_BUILD)/crt.o $(THREAD_POOL_CLANG_BUILD)/cxx/lib/libc++.a $(THREAD_POOL_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(THREAD_POOL_DIR)/thread-pool.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(THREAD_POOL_BUILD)/probe.o" "$(THREAD_POOL_BUILD)/thread-pool.o" "$(THREAD_POOL_BUILD)/threading.o" \
		"$(THREAD_POOL_BUILD)/crt.o" "$(THREAD_POOL_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(THREAD_POOL_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-thread-pool: install-libc aux-test-runner thread-pool-fixture
	bash "$(THREAD_POOL_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(THREAD_POOL_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(THREAD_POOL_BUILD)/probe.d $(THREAD_POOL_BUILD)/thread-pool.d $(THREAD_POOL_BUILD)/threading.d
