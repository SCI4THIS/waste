# Production Clang module task owner; actual cache, LLVM tasks and shared sleep.
MODULE_TASKS_DIR := $(REPO_ROOT)/src/system-tests/clang/module-tasks
MODULE_TASKS_BUILD := $(abspath $(BUILD_DIR))/clang/module-tasks
MODULE_TASKS_SOURCE := $(MODULE_CACHE_SOURCE)
MODULE_TASKS_GUEST := $(MODULE_CACHE_GUEST)
MODULE_TASKS_INPUTS := $(MODULE_TASKS_DIR)/module-tasks.mk $(SDK_HEADERS) $(REPO_ROOT)/src/aux/clang/clang-waste.patch
MODULE_TASKS_OBJECTS := $(MODULE_TASKS_BUILD)/probe.o $(MODULE_CACHE_BUILD)/cache.o \
	$(MODULE_CACHE_BUILD)/memory.o $(MODULE_CACHE_BUILD)/serialization.o $(MODULE_CACHE_BUILD)/logger.o \
	$(MODULE_CACHE_BUILD)/crt.o

.PHONY: module-tasks-fixture module-tasks-native-source module-tasks-guest-source test-module-tasks
module-tasks-fixture: $(MODULE_TASKS_BUILD)/probe.wasm

$(MODULE_TASKS_BUILD)/probe.o: $(MODULE_TASKS_DIR)/probe.cpp $(MODULE_TASKS_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) \
		-I "$(MODULE_TASKS_SOURCE)/clang/lib/DependencyScanning" -c "$<" -o "$@"

$(MODULE_TASKS_BUILD)/probe.wasm: $(MODULE_TASKS_OBJECTS) $(MODULE_CACHE_SUPPORT) \
	$(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm \
	$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(MODULE_TASKS_DIR)/module-tasks.mk
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main --export=__stack_pointer \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		$(MODULE_TASKS_OBJECTS) "$(MODULE_CACHE_SUPPORT)" "$(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" "$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-module-tasks: install-libc module-tasks-fixture
	$(MAKE) -C ../cli-rt guest-session guest-session-sanitize
	$(PYTHON) "$(MODULE_TASKS_DIR)/native-session.py" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(MODULE_TASKS_BUILD)" "$(MODULE_CACHE_SESSION_RUNNER)"

module-tasks-native-source: | module-cache-build-inputs
	mkdir -p "$(MODULE_TASKS_BUILD)/native"
	for profile in native threaded; do \
		flag=; test "$$profile" != threaded || flag=-DWASTE_TEST_THREADED_BRANCH; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_TASKS_SOURCE)/llvm/include" -isystem "$(MODULE_TASKS_SOURCE)/clang/include" \
			-isystem "$(MODULE_TASKS_GUEST)/tools/clang/include" \
			-I "$(MODULE_TASKS_SOURCE)/clang/lib/DependencyScanning" \
			-fsyntax-only "$(MODULE_TASKS_DIR)/native.cpp" || exit; \
	done

# Exact production CMake/PCH rule, not a copied worker implementation.
module-tasks-guest-source: | module-cache-build-inputs
	$(MAKE) -C "$(MODULE_TASKS_GUEST)" -f tools/clang/lib/DependencyScanning/CMakeFiles/obj.clangDependencyScanning.dir/build.make \
		tools/clang/lib/DependencyScanning/CMakeFiles/obj.clangDependencyScanning.dir/DependencyScanningWorker.cpp.obj -j1

-include $(MODULE_TASKS_BUILD)/probe.d
