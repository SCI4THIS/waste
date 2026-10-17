# Actual ThinBackendProc error ownership and accepted LLVM deferred execution.
LTO_ERRORS_DIR := $(REPO_ROOT)/src/system-tests/clang/lto-errors
LTO_ERRORS_BUILD := $(abspath $(BUILD_DIR))/clang/lto-errors
LTO_ERRORS_INPUTS := $(LTO_ERRORS_DIR)/lto-errors.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch
LTO_ERRORS_INIT_OBJECTS := $(MODULE_CACHE_GUEST)/lib/MC/CMakeFiles/LLVMMC.dir/MCTargetOptions.cpp.obj \
	$(MODULE_CACHE_GUEST)/lib/Passes/CMakeFiles/LLVMPasses.dir/PassBuilderPipelines.cpp.obj \
	$(MODULE_CACHE_GUEST)/lib/Passes/CMakeFiles/LLVMPasses.dir/PassesOptions.cpp.obj \
	$(MODULE_CACHE_GUEST)/lib/Transforms/Scalar/CMakeFiles/LLVMScalarOpts.dir/LICM.cpp.obj \
	$(MODULE_CACHE_GUEST)/lib/Transforms/Scalar/CMakeFiles/LLVMScalarOpts.dir/LoopUnrollPass.cpp.obj \
	$(MODULE_CACHE_GUEST)/lib/Transforms/Scalar/CMakeFiles/LLVMScalarOpts.dir/ScalarOptions.cpp.obj
LTO_ERRORS_OBJECTS := $(LTO_ERRORS_BUILD)/probe.o $(MODULE_CACHE_BUILD)/crt.o $(LTO_ERRORS_INIT_OBJECTS)
LTO_ERRORS_OPTION := $(MODULE_CACHE_GUEST)/lib/libLLVMOption.a

.PHONY: lto-errors-fixture lto-errors-native-source lto-errors-guest-source test-lto-errors
lto-errors-fixture: $(LTO_ERRORS_BUILD)/probe.wasm

# Config's real target/pipeline constructors read real generated option owners.
.PHONY: lto-errors-init-inputs
lto-errors-init-inputs: | module-cache-build-inputs
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" analysis_gen PassesOptionsTableGen ScalarOptionsTableGen LLVMOption -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/CodeGen/CMakeFiles/LLVMCodeGen.dir/build.make \
		lib/CodeGen/CMakeFiles/LLVMCodeGen.dir/depend -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/CodeGen/CMakeFiles/LLVMCodeGen.dir/build.make \
		lib/CodeGen/CMakeFiles/LLVMCodeGen.dir/cmake_pch.hxx.pch -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/IR/CMakeFiles/LLVMCore.dir/build.make \
		lib/IR/CMakeFiles/LLVMCore.dir/depend -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/IR/CMakeFiles/LLVMCore.dir/build.make \
		lib/IR/CMakeFiles/LLVMCore.dir/cmake_pch.hxx.pch -j1
	for component in MC/CMakeFiles/LLVMMC Passes/CMakeFiles/LLVMPasses Transforms/Scalar/CMakeFiles/LLVMScalarOpts; do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f "lib/$$component.dir/build.make" \
			"lib/$$component.dir/depend" -j1 || exit; \
	done

$(LTO_ERRORS_OPTION): | lto-errors-init-inputs
	test -f "$@"

$(LTO_ERRORS_INIT_OBJECTS): $(LTO_ERRORS_INPUTS) $(MODULE_CACHE_GUEST)/.configured | lto-errors-init-inputs
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f "$(dir $@)build.make" "$(patsubst $(MODULE_CACHE_GUEST)/%,%,$@)" -j1

$(word 1,$(LTO_ERRORS_INIT_OBJECTS)): $(MODULE_CACHE_SOURCE)/llvm/lib/MC/MCTargetOptions.cpp
$(word 2,$(LTO_ERRORS_INIT_OBJECTS)): $(MODULE_CACHE_SOURCE)/llvm/lib/Passes/PassBuilderPipelines.cpp
$(word 3,$(LTO_ERRORS_INIT_OBJECTS)): $(MODULE_CACHE_SOURCE)/llvm/lib/Passes/PassesOptions.cpp
$(word 4,$(LTO_ERRORS_INIT_OBJECTS)): $(MODULE_CACHE_SOURCE)/llvm/lib/Transforms/Scalar/LICM.cpp
$(word 5,$(LTO_ERRORS_INIT_OBJECTS)): $(MODULE_CACHE_SOURCE)/llvm/lib/Transforms/Scalar/LoopUnrollPass.cpp
$(word 6,$(LTO_ERRORS_INIT_OBJECTS)): $(MODULE_CACHE_SOURCE)/llvm/lib/Transforms/Scalar/ScalarOptions.cpp

$(LTO_ERRORS_BUILD)/probe.o: $(LTO_ERRORS_DIR)/probe.cpp $(LTO_ERRORS_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter -c "$<" -o "$@"

$(LTO_ERRORS_BUILD)/probe.wasm: $(LTO_ERRORS_OBJECTS) $(LTO_ERRORS_OPTION) $(MODULE_CACHE_SUPPORT) \
	$(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm \
	$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(LTO_ERRORS_DIR)/lto-errors.mk
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main --export=__stack_pointer \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		$(LTO_ERRORS_OBJECTS) "$(LTO_ERRORS_OPTION)" "$(MODULE_CACHE_SUPPORT)" "$(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" "$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-lto-errors: install-libc lto-errors-fixture
	$(MAKE) -C ../cli-rt guest-session guest-session-sanitize
	$(PYTHON) "$(LTO_ERRORS_DIR)/native-session.py" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(LTO_ERRORS_BUILD)" "$(MODULE_CACHE_SESSION_RUNNER)"

lto-errors-native-source: | module-cache-build-inputs
	for profile in native threaded; do \
		flag=; test "$$profile" != threaded || flag=-DWASTE_TEST_THREADED_BRANCH; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" -isystem "$(MODULE_CACHE_GUEST)/include" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/llvm/lib/LTO/LTO.cpp\" \
			-fsyntax-only "$(LTO_ERRORS_DIR)/native.cpp" || exit; \
	done

# Actual LTO source/PCH rules, including both production addError callers.
lto-errors-guest-source: lld-common-guest-source
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" analysis_gen -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/LTO/CMakeFiles/LLVMLTO.dir/build.make \
		lib/LTO/CMakeFiles/LLVMLTO.dir/depend -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/LTO/CMakeFiles/LLVMLTO.dir/build.make \
		lib/LTO/CMakeFiles/LLVMLTO.dir/LTO.cpp.obj -j1

# Broader production attempt; report the next real port boundary if it fails.
.PHONY: lto-component-guest-source
lto-component-guest-source: lto-errors-guest-source
	objects=$$(sed -n 's/^\(lib\/LTO\/[^:]*\.cpp\.obj\): .*flags.make$$/\1/p' \
		"$(MODULE_CACHE_GUEST)/lib/LTO/CMakeFiles/LLVMLTO.dir/build.make"); \
		test -n "$$objects" || exit 1; \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/LTO/CMakeFiles/LLVMLTO.dir/build.make $$objects -j1

-include $(wildcard $(LTO_ERRORS_BUILD)/*.d)
