# Actual LLD filesystem helpers and LLVM output ownership.
LLD_FILESYSTEM_DIR := $(REPO_ROOT)/src/aux/clang/test/lld-filesystem
LLD_FILESYSTEM_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/lld-filesystem
LLD_FILESYSTEM_INPUTS := $(LLD_FILESYSTEM_DIR)/lld-filesystem.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch
LLD_FILESYSTEM_OBJECTS := $(LLD_FILESYSTEM_BUILD)/probe.o $(LLD_FILESYSTEM_BUILD)/filesystem.o $(LLD_FILESYSTEM_BUILD)/pointer-formats.o \
	$(filter-out $(LLD_DIAGNOSTICS_BUILD)/probe.o,$(LLD_DIAGNOSTICS_OBJECTS))

.PHONY: lld-filesystem-fixture lld-filesystem-native-source lld-filesystem-guest-source test-lld-filesystem
lld-filesystem-fixture: $(LLD_FILESYSTEM_BUILD)/probe.wasm

$(LLD_FILESYSTEM_BUILD)/probe.o: $(LLD_FILESYSTEM_DIR)/probe.cpp $(LLD_FILESYSTEM_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(LLD_DIAGNOSTICS_CXXFLAGS) -c "$<" -o "$@"
$(LLD_FILESYSTEM_BUILD)/pointer-formats.o: $(LLD_FILESYSTEM_DIR)/pointer-formats.c $(LLD_FILESYSTEM_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CC) $(MODULE_CACHE_CFLAGS) -std=c11 -Wall -Wextra -Werror -MD -MP -c "$<" -o "$@"
$(LLD_FILESYSTEM_BUILD)/filesystem.o: $(LLD_FILESYSTEM_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(LLD_DIAGNOSTICS_CXXFLAGS) -c "$(MODULE_CACHE_SOURCE)/lld/Common/Filesystem.cpp" -o "$@"

$(LLD_FILESYSTEM_BUILD)/probe.wasm: $(LLD_FILESYSTEM_OBJECTS) $(MODULE_CACHE_SUPPORT) \
	$(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm \
	$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(LLD_FILESYSTEM_DIR)/lld-filesystem.mk
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main --export=__stack_pointer \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		$(LLD_FILESYSTEM_OBJECTS) "$(MODULE_CACHE_SUPPORT)" "$(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" "$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-lld-filesystem: install-libc lld-filesystem-fixture
	$(MAKE) -C ../cli-rt guest-session guest-session-sanitize
	$(PYTHON) "$(LLD_FILESYSTEM_DIR)/native-session.py" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(LLD_FILESYSTEM_BUILD)" "$(MODULE_CACHE_SESSION_RUNNER)"

lld-filesystem-native-source: | module-cache-build-inputs
	for profile in native threaded; do \
		flag=; test "$$profile" != threaded || flag=-DWASTE_TEST_THREADED_BRANCH; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" -isystem "$(MODULE_CACHE_SOURCE)/lld/include" \
			-isystem "$(MODULE_CACHE_GUEST)/tools/lld/include" -I "$(MODULE_CACHE_SOURCE)/lld/Common" \
			-fsyntax-only "$(LLD_FILESYSTEM_DIR)/native.cpp" || exit; \
	done

# Compile the exact production rules, retaining LLVM's real CodeGen PCH.
lld-filesystem-guest-source: lld-diagnostics-guest-source
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f tools/lld/Common/CMakeFiles/lldCommon.dir/build.make \
		tools/lld/Common/CMakeFiles/lldCommon.dir/Filesystem.cpp.obj -j1

-include $(wildcard $(LLD_FILESYSTEM_BUILD)/*.d)

# Enumerate actual generated CMake object rules, not a copied source inventory.
.PHONY: lld-common-guest-source lld-wasm-guest-source
lld-common-guest-source: lld-filesystem-guest-source
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" llvm_vcsrevision_h -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f tools/lld/Common/CMakeFiles/lldCommon.dir/build.make \
		tools/lld/Common/VCSVersion.inc -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f tools/lld/Common/CMakeFiles/lldCommon.dir/build.make \
		tools/lld/Common/CMakeFiles/lldCommon.dir/depend -j1
	objects=$$(sed -n 's/^\(tools\/lld\/Common\/[^:]*\.cpp\.obj\): .*flags.make$$/\1/p' \
		"$(MODULE_CACHE_GUEST)/tools/lld/Common/CMakeFiles/lldCommon.dir/build.make"); \
		test -n "$$objects" || exit 1; \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f tools/lld/Common/CMakeFiles/lldCommon.dir/build.make $$objects -j1

lld-wasm-guest-source: lld-common-guest-source
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" WasmOptionsTableGen analysis_gen -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/IR/CMakeFiles/LLVMCore.dir/build.make \
		lib/IR/CMakeFiles/LLVMCore.dir/depend -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/IR/CMakeFiles/LLVMCore.dir/build.make \
		lib/IR/CMakeFiles/LLVMCore.dir/cmake_pch.hxx.pch -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f tools/lld/wasm/CMakeFiles/lldWasm.dir/build.make \
		tools/lld/wasm/CMakeFiles/lldWasm.dir/depend -j1
	objects=$$(sed -n 's/^\(tools\/lld\/wasm\/[^:]*\.cpp\.obj\): .*flags.make$$/\1/p' \
		"$(MODULE_CACHE_GUEST)/tools/lld/wasm/CMakeFiles/lldWasm.dir/build.make"); \
		test -n "$$objects" || exit 1; \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f tools/lld/wasm/CMakeFiles/lldWasm.dir/build.make $$objects -j1
