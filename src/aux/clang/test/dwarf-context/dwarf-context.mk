# Actual DWARF context cache ownership and full production source gates.
DWARF_CONTEXT_DIR := $(REPO_ROOT)/src/aux/clang/test/dwarf-context
DWARF_CONTEXT_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/dwarf-context
DWARF_CONTEXT_ARCHIVE_STAMP := $(DWARF_CONTEXT_BUILD)/.archive-checked
DWARF_CONTEXT_INPUTS := $(DWARF_CONTEXT_DIR)/dwarf-context.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch $(REPO_ROOT)/src/aux/clang/clang.mk \
	$(wildcard $(REPO_ROOT)/src/aux/clang/*.cmake)
DWARF_CONTEXT_OBJECTS := $(DWARF_CONTEXT_BUILD)/probe.o $(MODULE_CACHE_BUILD)/crt.o
DWARF_CONTEXT_ARCHIVES := $(addprefix $(MODULE_CACHE_GUEST)/lib/libLLVM,\
	DebugInfoDWARF.a DebugInfoDWARFLowLevel.a Object.a MCParser.a MC.a \
	IRReader.a AsmParser.a BitReader.a Core.a TextAPI.a FrontendHLSL.a \
	Remarks.a BitstreamReader.a BinaryFormat.a TargetParser.a Option.a Support.a Demangle.a)
# Reuse existing producers for shared Support/Demangle/TargetParser/Option files.
# Giving those targets a context prerequisite would expand other fixture builds.
DWARF_CONTEXT_OWN_ARCHIVES := $(filter-out $(MODULE_CACHE_SUPPORT) \
	$(MODULE_CACHE_CORE_ARCHIVES) \
	$(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a $(TARGET_HOST_ARCHIVE) \
	$(MODULE_CACHE_GUEST)/lib/libLLVMOption.a,$(DWARF_CONTEXT_ARCHIVES))

.PHONY: dwarf-context-fixture dwarf-context-build-inputs dwarf-context-native-source dwarf-context-guest-source test-dwarf-context
dwarf-context-fixture: $(DWARF_CONTEXT_BUILD)/probe.wasm
dwarf-context-build-inputs: $(DWARF_CONTEXT_ARCHIVE_STAMP)

$(DWARF_CONTEXT_ARCHIVE_STAMP): $(DWARF_CONTEXT_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MAKE) -C ../aux clang-dwarf CLANG_JOBS=1
	@for archive in $(DWARF_CONTEXT_ARCHIVES); do test -s "$$archive" || exit; done
	touch "$@"

$(DWARF_CONTEXT_OWN_ARCHIVES): | dwarf-context-build-inputs
	test -s "$@" || $(MAKE) -C ../aux clang-dwarf CLANG_JOBS=1

$(DWARF_CONTEXT_BUILD)/probe.o: $(DWARF_CONTEXT_DIR)/probe.cpp $(DWARF_CONTEXT_INPUTS) | dwarf-context-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter \
		-I "$(MODULE_CACHE_SOURCE)/llvm/lib/DebugInfo/DWARF" -c "$<" -o "$@"

$(DWARF_CONTEXT_BUILD)/probe.wasm: $(DWARF_CONTEXT_OBJECTS) $(DWARF_CONTEXT_ARCHIVE_STAMP) $(DWARF_CONTEXT_ARCHIVES) \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start \
		--export=main --export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		$(DWARF_CONTEXT_OBJECTS) $(DWARF_CONTEXT_ARCHIVES) \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

dwarf-context-native-source: | module-cache-build-inputs
	for source in DebugInfo/DWARF/DWARFContext MC/MCPseudoProbe MC/MCParser/AsmParser MC/MCParser/MasmParser; do \
	for profile in native threaded; do \
		flag=; test "$$profile" != threaded || flag=-DWASTE_TEST_THREADED_BRANCH; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" \
			-I "$(MODULE_CACHE_SOURCE)/llvm/lib/DebugInfo/DWARF" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/llvm/lib/$$source.cpp\" \
			-fsyntax-only "$(DWARF_CONTEXT_DIR)/native.cpp" || exit; \
	done; done

dwarf-context-guest-source: | module-cache-build-inputs
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/DebugInfo/DWARF/CMakeFiles/LLVMDebugInfoDWARF.dir/build.make \
		lib/DebugInfo/DWARF/CMakeFiles/LLVMDebugInfoDWARF.dir/depend -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/DebugInfo/DWARF/CMakeFiles/LLVMDebugInfoDWARF.dir/build.make \
		lib/DebugInfo/DWARF/CMakeFiles/LLVMDebugInfoDWARF.dir/DWARFContext.cpp.obj -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/MC/CMakeFiles/LLVMMC.dir/build.make \
		lib/MC/CMakeFiles/LLVMMC.dir/depend -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/MC/CMakeFiles/LLVMMC.dir/build.make \
		lib/MC/CMakeFiles/LLVMMC.dir/MCPseudoProbe.cpp.obj -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/MC/MCParser/CMakeFiles/LLVMMCParser.dir/build.make \
		lib/MC/MCParser/CMakeFiles/LLVMMCParser.dir/depend -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/MC/MCParser/CMakeFiles/LLVMMCParser.dir/build.make \
		lib/MC/MCParser/CMakeFiles/LLVMMCParser.dir/AsmParser.cpp.obj \
		lib/MC/MCParser/CMakeFiles/LLVMMCParser.dir/MasmParser.cpp.obj -j1

test-dwarf-context: install-libc aux-test-runner dwarf-context-fixture
	bash "$(DWARF_CONTEXT_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(DWARF_CONTEXT_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(wildcard $(DWARF_CONTEXT_BUILD)/*.d)
