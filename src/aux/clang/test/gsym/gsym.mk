# Actual DWARF context cache ownership and full production source gates.
GSYM_DIR := $(REPO_ROOT)/src/aux/clang/test/gsym
GSYM_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/gsym
GSYM_ARCHIVE_STAMP := $(GSYM_BUILD)/.archive-checked
GSYM_INPUTS := $(GSYM_DIR)/gsym.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch $(REPO_ROOT)/src/aux/clang/clang.mk \
	$(wildcard $(REPO_ROOT)/src/aux/clang/*.cmake)
GSYM_OBJECTS := $(GSYM_BUILD)/probe.o $(MODULE_CACHE_BUILD)/crt.o
GSYM_ARCHIVES := $(addprefix $(MODULE_CACHE_GUEST)/lib/libLLVM,\
	DebugInfoGSYM.a DebugInfoDWARF.a DebugInfoDWARFLowLevel.a Object.a MCParser.a MC.a \
	IRReader.a AsmParser.a BitReader.a Core.a TextAPI.a FrontendHLSL.a \
	Remarks.a BitstreamReader.a BinaryFormat.a TargetParser.a Option.a Support.a Demangle.a)
# Reuse existing producers for the shared Support/TargetParser/Option files.
# Giving those targets a context prerequisite would expand other fixture builds.
GSYM_OWN_ARCHIVES := $(MODULE_CACHE_GUEST)/lib/libLLVMDebugInfoGSYM.a

.PHONY: gsym-fixture gsym-build-inputs gsym-native-source gsym-guest-source test-gsym
gsym-fixture: $(GSYM_BUILD)/probe.wasm
gsym-build-inputs: $(GSYM_ARCHIVE_STAMP)

$(GSYM_ARCHIVE_STAMP): $(GSYM_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MAKE) -C ../aux clang-gsym CLANG_JOBS=1
	@for archive in $(GSYM_ARCHIVES); do test -s "$$archive" || exit; done
	touch "$@"

$(GSYM_OWN_ARCHIVES): | gsym-build-inputs
	test -s "$@" || $(MAKE) -C ../aux clang-gsym CLANG_JOBS=1

$(GSYM_BUILD)/probe.o: $(GSYM_DIR)/probe.cpp $(GSYM_INPUTS) | gsym-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter \
		-I "$(MODULE_CACHE_SOURCE)/llvm/lib/DebugInfo/GSYM" -c "$<" -o "$@"

$(GSYM_BUILD)/probe.wasm: $(GSYM_OBJECTS) $(GSYM_ARCHIVE_STAMP) $(GSYM_ARCHIVES) \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start \
		--export=main --export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		$(GSYM_OBJECTS) $(GSYM_ARCHIVES) \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

gsym-native-source: | module-cache-build-inputs
	for source in DebugInfo/GSYM/DwarfTransformer DebugInfo/GSYM/GsymCreator DebugInfo/GSYM/GsymCreatorV1 DebugInfo/GSYM/GsymCreatorV2; do \
	for profile in native threaded; do \
		flag=; test "$$profile" != threaded || flag=-DWASTE_TEST_THREADED_BRANCH; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" \
			-I "$(MODULE_CACHE_SOURCE)/llvm/lib/DebugInfo/GSYM" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/llvm/lib/$$source.cpp\" \
			-fsyntax-only "$(GSYM_DIR)/native.cpp" || exit; \
	done; done

gsym-guest-source: | module-cache-build-inputs
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/DebugInfo/GSYM/CMakeFiles/LLVMDebugInfoGSYM.dir/build.make lib/DebugInfo/GSYM/CMakeFiles/LLVMDebugInfoGSYM.dir/depend
	@set -eu; for source in DwarfTransformer GsymCreator GsymCreatorV1 GsymCreatorV2; do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/DebugInfo/GSYM/CMakeFiles/LLVMDebugInfoGSYM.dir/build.make \
			"lib/DebugInfo/GSYM/CMakeFiles/LLVMDebugInfoGSYM.dir/$$source.cpp.obj"; \
	done

test-gsym: install-libc aux-test-runner gsym-fixture
	bash "$(GSYM_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(GSYM_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(wildcard $(GSYM_BUILD)/*.d)
