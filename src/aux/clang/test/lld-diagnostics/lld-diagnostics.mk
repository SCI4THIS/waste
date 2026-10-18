# Actual LLD diagnostics/context, LLVM regex and production Wasm driver selection.
LLD_DIAGNOSTICS_DIR := $(REPO_ROOT)/src/aux/clang/test/lld-diagnostics
LLD_DIAGNOSTICS_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/lld-diagnostics
LLD_DIAGNOSTICS_INPUTS := $(LLD_DIAGNOSTICS_DIR)/lld-diagnostics.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch \
	$(REPO_ROOT)/src/aux/clang/include/lld/Common/WasteDiagnostics.h
LLD_DIAGNOSTICS_CXXFLAGS := $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter \
	-isystem "$(MODULE_CACHE_SOURCE)/lld/include" -isystem "$(MODULE_CACHE_GUEST)/tools/lld/include"
LLD_DIAGNOSTICS_OBJECTS := $(addprefix $(LLD_DIAGNOSTICS_BUILD)/,probe.o diagnostics.o context.o memory.o codegen-flags.o mc-flags.o) \
	$(MODULE_CACHE_BUILD)/crt.o

.PHONY: lld-diagnostics-fixture lld-diagnostics-native-source lld-diagnostics-guest-source test-lld-diagnostics
lld-diagnostics-fixture: $(LLD_DIAGNOSTICS_BUILD)/probe.wasm

$(LLD_DIAGNOSTICS_BUILD)/probe.o: $(LLD_DIAGNOSTICS_DIR)/probe.cpp $(LLD_DIAGNOSTICS_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(LLD_DIAGNOSTICS_CXXFLAGS) -c "$<" -o "$@"

# Compile complete source units; section GC removes unrelated IR diagnostic code.
define LLD_DIAGNOSTICS_UNIT
$(LLD_DIAGNOSTICS_BUILD)/$(1).o: $(LLD_DIAGNOSTICS_INPUTS) | module-cache-build-inputs
	mkdir -p "$$(@D)"
	$(MODULE_CACHE_CXX) $(LLD_DIAGNOSTICS_CXXFLAGS) -c "$(MODULE_CACHE_SOURCE)/$(2).cpp" -o "$$@"
endef
$(eval $(call LLD_DIAGNOSTICS_UNIT,diagnostics,lld/Common/ErrorHandler))
$(eval $(call LLD_DIAGNOSTICS_UNIT,context,lld/Common/CommonLinkerContext))
$(eval $(call LLD_DIAGNOSTICS_UNIT,memory,lld/Common/Memory))
$(eval $(call LLD_DIAGNOSTICS_UNIT,codegen-flags,llvm/lib/CodeGen/CommandFlags))
$(eval $(call LLD_DIAGNOSTICS_UNIT,mc-flags,llvm/lib/MC/MCTargetOptionsCommandFlags))

$(LLD_DIAGNOSTICS_BUILD)/probe.wasm: $(LLD_DIAGNOSTICS_OBJECTS) $(MODULE_CACHE_SUPPORT) \
	$(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm \
	$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(LLD_DIAGNOSTICS_DIR)/lld-diagnostics.mk
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main --export=__stack_pointer \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		$(LLD_DIAGNOSTICS_OBJECTS) "$(MODULE_CACHE_SUPPORT)" "$(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" "$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-lld-diagnostics: install-libc lld-diagnostics-fixture
	$(MAKE) -C ../cli-rt guest-session guest-session-sanitize
	$(PYTHON) "$(LLD_DIAGNOSTICS_DIR)/native-session.py" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(LLD_DIAGNOSTICS_BUILD)" "$(MODULE_CACHE_SESSION_RUNNER)"

lld-diagnostics-native-source: | module-cache-build-inputs
	for profile in native threaded; do \
		flag=; test "$$profile" != threaded || flag=-DWASTE_TEST_THREADED_BRANCH; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" -isystem "$(MODULE_CACHE_SOURCE)/lld/include" \
			-isystem "$(MODULE_CACHE_GUEST)/tools/lld/include" -I "$(MODULE_CACHE_SOURCE)/lld/Common" \
			-fsyntax-only "$(LLD_DIAGNOSTICS_DIR)/native.cpp" || exit; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" -isystem "$(MODULE_CACHE_SOURCE)/lld/include" \
			-I "$(MODULE_CACHE_SOURCE)/lld/tools/lld" \
			-fsyntax-only "$(LLD_DIAGNOSTICS_DIR)/native-driver.cpp" || exit; \
	done

# Use exact production CMake rules and PCH, retaining the full translation units.
lld-diagnostics-guest-source: | module-cache-build-inputs
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" vt_gen -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/CodeGen/CMakeFiles/LLVMCodeGen.dir/build.make \
		lib/CodeGen/CMakeFiles/LLVMCodeGen.dir/depend -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/CodeGen/CMakeFiles/LLVMCodeGen.dir/build.make \
		lib/CodeGen/CMakeFiles/LLVMCodeGen.dir/cmake_pch.hxx.pch -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f tools/lld/Common/CMakeFiles/lldCommon.dir/build.make \
		tools/lld/Common/CMakeFiles/lldCommon.dir/depend -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f tools/lld/Common/CMakeFiles/lldCommon.dir/build.make \
		tools/lld/Common/CMakeFiles/lldCommon.dir/ErrorHandler.cpp.obj \
		tools/lld/Common/CMakeFiles/lldCommon.dir/CommonLinkerContext.cpp.obj -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f tools/lld/tools/lld/CMakeFiles/lld.dir/build.make \
		tools/lld/tools/lld/CMakeFiles/lld.dir/depend -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f tools/lld/tools/lld/CMakeFiles/lld.dir/build.make \
		tools/lld/tools/lld/CMakeFiles/lld.dir/lld.cpp.obj -j1

-include $(wildcard $(LLD_DIAGNOSTICS_BUILD)/*.d)
