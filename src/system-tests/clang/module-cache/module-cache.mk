# Actual Clang cache ownership and compiler-local timed synchronization.
MODULE_CACHE_DIR := $(REPO_ROOT)/src/system-tests/clang/module-cache
MODULE_CACHE_BUILD := $(abspath $(BUILD_DIR))/clang/module-cache
MODULE_CACHE_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
MODULE_CACHE_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
MODULE_CACHE_SOURCE := $(MODULE_CACHE_CLANG_BUILD)/source
MODULE_CACHE_GUEST := $(MODULE_CACHE_CLANG_BUILD)/guest
MODULE_CACHE_SUPPORT := $(MODULE_CACHE_GUEST)/lib/libLLVMSupport.a
MODULE_CACHE_CC ?= clang
MODULE_CACHE_CXX ?= clang++
MODULE_CACHE_SESSION_RUNNER ?= $(REPO_ROOT)/build/cli-rt/private/guest-session$(if $(findstring sanitize,$(WASM_RUNNER)),-sanitize,)
MODULE_CACHE_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
MODULE_CACHE_CXXFLAGS := -isystem "$(MODULE_CACHE_CLANG_BUILD)/cxx/include/c++/v1" \
	$(MODULE_CACHE_CFLAGS) -isystem "$(MODULE_CACHE_CLANG_DIR)/include" \
	-isystem "$(MODULE_CACHE_GUEST)/include" -isystem "$(MODULE_CACHE_SOURCE)/llvm/include" \
	-isystem "$(MODULE_CACHE_SOURCE)/clang/include" -isystem "$(MODULE_CACHE_GUEST)/tools/clang/include" \
	-std=c++17 -DNDEBUG -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -MD -MP
MODULE_CACHE_INPUTS := $(MODULE_CACHE_DIR)/module-cache.mk $(SDK_HEADERS) \
	$(MODULE_CACHE_CLANG_DIR)/clang-waste.patch \
	$(MODULE_CACHE_CLANG_DIR)/include/clang/Basic/WasteSynchronization.h
MODULE_CACHE_OBJECTS := $(addprefix $(MODULE_CACHE_BUILD)/,probe.o cache.o memory.o serialization.o logger.o crt.o)

# Shared IR archives have their own producer. A small IR observer must not
# inherit the DWARF fixture's larger dependency graph through these filenames.
MODULE_CACHE_CORE_ARCHIVES := $(addprefix $(MODULE_CACHE_GUEST)/lib/libLLVM,\
	Core.a Remarks.a BitstreamReader.a BinaryFormat.a)
MODULE_CACHE_CORE_STAMP := $(MODULE_CACHE_BUILD)/.core-archives-checked

.PHONY: module-cache-core-build-inputs
module-cache-core-build-inputs: $(MODULE_CACHE_CORE_STAMP)
$(MODULE_CACHE_CORE_STAMP): $(MODULE_CACHE_INPUTS) $(REPO_ROOT)/src/aux/clang/clang.mk \
	$(REPO_ROOT)/src/config.h | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MAKE) -C ../aux clang-core CLANG_JOBS=1
	@for archive in $(MODULE_CACHE_CORE_ARCHIVES); do test -s "$$archive" || exit; done
	touch "$@"
$(MODULE_CACHE_CORE_ARCHIVES): | module-cache-core-build-inputs
	test -s "$@" || $(MAKE) -C ../aux clang-core CLANG_JOBS=1

.PHONY: module-cache-fixture module-cache-build-inputs module-cache-native-source module-cache-guest-source test-module-cache
module-cache-fixture: $(MODULE_CACHE_BUILD)/probe.wasm
module-cache-build-inputs:
	$(MAKE) -C ../aux clang-support CLANG_JOBS=1

# LLVMSupport builds Demangle too. Keep its producer independent of DWARF
# fixture stamps so small Support-only observers do not expand that graph.
$(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a: | module-cache-build-inputs
	test -s "$@"

$(MODULE_CACHE_BUILD)/probe.o: $(MODULE_CACHE_DIR)/probe.cpp $(MODULE_CACHE_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -c "$<" -o "$@"

$(MODULE_CACHE_BUILD)/cache.o: $(MODULE_CACHE_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter \
		-c "$(MODULE_CACHE_SOURCE)/clang/lib/DependencyScanning/InProcessModuleCache.cpp" -o "$@"
$(MODULE_CACHE_BUILD)/memory.o: $(MODULE_CACHE_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter \
		-c "$(MODULE_CACHE_SOURCE)/clang/lib/Serialization/InMemoryModuleCache.cpp" -o "$@"
$(MODULE_CACHE_BUILD)/serialization.o: $(MODULE_CACHE_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter \
		-c "$(MODULE_CACHE_SOURCE)/clang/lib/Serialization/ModuleCache.cpp" -o "$@"
$(MODULE_CACHE_BUILD)/logger.o: $(MODULE_CACHE_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter \
		-c "$(MODULE_CACHE_SOURCE)/clang/lib/Basic/AtomicLineLogger.cpp" -o "$@"

$(MODULE_CACHE_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(MODULE_CACHE_DIR)/module-cache.mk
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CC) $(MODULE_CACHE_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(MODULE_CACHE_BUILD)/probe.wasm: $(MODULE_CACHE_OBJECTS) $(MODULE_CACHE_SUPPORT) \
	$(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm \
	$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main --export=__stack_pointer \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		$(MODULE_CACHE_OBJECTS) "$(MODULE_CACHE_SUPPORT)" "$(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" "$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-module-cache: install-libc module-cache-fixture
	$(MAKE) -C ../cli-rt guest-session guest-session-sanitize
	$(PYTHON) "$(MODULE_CACHE_DIR)/native-session.py" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(MODULE_CACHE_BUILD)" "$(MODULE_CACHE_SESSION_RUNNER)"

module-cache-native-source: | module-cache-build-inputs
	mkdir -p "$(MODULE_CACHE_BUILD)/native"
	$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
		-fno-exceptions -fno-rtti -isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
		-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" -isystem "$(MODULE_CACHE_SOURCE)/clang/include" \
		-isystem "$(MODULE_CACHE_GUEST)/tools/clang/include" \
		-I "$(MODULE_CACHE_SOURCE)/clang/lib/DependencyScanning" \
		-c "$(MODULE_CACHE_DIR)/native.cpp" -o "$(MODULE_CACHE_BUILD)/native/cache.o"
	$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
		-DWASTE_TEST_THREADED_BRANCH -fno-exceptions -fno-rtti \
		-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
		-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" -isystem "$(MODULE_CACHE_SOURCE)/clang/include" \
		-isystem "$(MODULE_CACHE_GUEST)/tools/clang/include" \
		-I "$(MODULE_CACHE_SOURCE)/clang/lib/DependencyScanning" \
		-c "$(MODULE_CACHE_DIR)/native.cpp" -o "$(MODULE_CACHE_BUILD)/native/cache-threaded.o"
	for source in DependencyScanning/DependencyScanningFilesystem Frontend/PrecompiledPreamble Driver/ModulesDriver; do \
		for profile in native threaded; do \
			flag=; test "$$profile" != threaded || flag=-DWASTE_TEST_THREADED_BRANCH; \
			$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
				$$flag -fno-exceptions -fno-rtti \
				-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
				-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" -isystem "$(MODULE_CACHE_SOURCE)/clang/include" \
				-isystem "$(MODULE_CACHE_GUEST)/tools/clang/include" \
				-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/clang/lib/$$source.cpp\" \
				-fsyntax-only "$(MODULE_CACHE_DIR)/native.cpp" || exit; \
		 done; \
	done

# Run the real generated CMake/PCH rules without waiting for the whole AST,
# frontend and driver graph. Keep one LLVM build owner and one job.
module-cache-guest-source: | module-cache-build-inputs
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f tools/clang/lib/AST/CMakeFiles/obj.clangAST.dir/build.make \
		tools/clang/lib/AST/CMakeFiles/obj.clangAST.dir/cmake_pch.hxx.pch -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f tools/clang/lib/DependencyScanning/CMakeFiles/obj.clangDependencyScanning.dir/build.make \
		tools/clang/lib/DependencyScanning/CMakeFiles/obj.clangDependencyScanning.dir/InProcessModuleCache.cpp.obj \
		tools/clang/lib/DependencyScanning/CMakeFiles/obj.clangDependencyScanning.dir/DependencyScanningFilesystem.cpp.obj -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f tools/clang/lib/Frontend/CMakeFiles/obj.clangFrontend.dir/build.make \
		tools/clang/lib/Frontend/CMakeFiles/obj.clangFrontend.dir/PrecompiledPreamble.cpp.obj -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f tools/clang/lib/Driver/CMakeFiles/obj.clangDriver.dir/build.make \
		tools/clang/lib/Driver/CMakeFiles/obj.clangDriver.dir/ModulesDriver.cpp.obj -j1

-include $(MODULE_CACHE_OBJECTS:.o=.d)
