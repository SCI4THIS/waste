# Production CFG node attributes, real IR owners and unprofiled frequencies.
CFG_FORMAT_DIR := $(REPO_ROOT)/src/aux/clang/test/cfg-format
CFG_FORMAT_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/cfg-format
CFG_FORMAT_INPUTS := $(CFG_FORMAT_DIR)/cfg-format.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch $(REPO_ROOT)/src/aux/clang/clang.mk \
	$(wildcard $(REPO_ROOT)/src/aux/clang/*.cmake)
CFG_FORMAT_SOURCE_NAMES := CFGPrinter HeatUtils BlockFrequencyInfo BlockFrequencyInfoImpl
CFG_FORMAT_SOURCE_RULE := lib/Analysis/CMakeFiles/LLVMAnalysis.dir
CFG_FORMAT_SOURCE_OBJECTS := $(addprefix $(MODULE_CACHE_GUEST)/$(CFG_FORMAT_SOURCE_RULE)/,\
	$(addsuffix .cpp.obj,$(CFG_FORMAT_SOURCE_NAMES)))
CFG_FORMAT_SOURCE_STAMP := $(CFG_FORMAT_BUILD)/.sources-checked
CFG_FORMAT_OBJECTS := $(CFG_FORMAT_BUILD)/probe.o $(MODULE_CACHE_BUILD)/crt.o
CFG_FORMAT_ARCHIVES := $(addprefix $(MODULE_CACHE_GUEST)/lib/libLLVM,\
	Core.a Remarks.a BitstreamReader.a BinaryFormat.a TargetParser.a Support.a Demangle.a)

.PHONY: cfg-format-fixture cfg-format-build-inputs cfg-format-guest-source cfg-format-native-source test-cfg-format
cfg-format-fixture: $(CFG_FORMAT_BUILD)/probe.wasm
cfg-format-build-inputs: $(CFG_FORMAT_SOURCE_STAMP)

$(CFG_FORMAT_SOURCE_STAMP): $(CFG_FORMAT_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(CFG_FORMAT_SOURCE_RULE)/build.make $(CFG_FORMAT_SOURCE_RULE)/depend
	@set -eu; for source in $(CFG_FORMAT_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(CFG_FORMAT_SOURCE_RULE)/build.make \
			"$(CFG_FORMAT_SOURCE_RULE)/$$source.cpp.obj"; \
	done
	touch "$@"

$(CFG_FORMAT_SOURCE_OBJECTS): | cfg-format-build-inputs
	test -s "$@" || $(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(CFG_FORMAT_SOURCE_RULE)/build.make "$(patsubst $(MODULE_CACHE_GUEST)/%,%,$@)"

$(CFG_FORMAT_BUILD)/probe.o: $(CFG_FORMAT_DIR)/probe.cpp $(CFG_FORMAT_INPUTS) | cfg-format-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter -c "$<" -o "$@"

$(CFG_FORMAT_BUILD)/probe.wasm: $(CFG_FORMAT_OBJECTS) $(CFG_FORMAT_SOURCE_STAMP) $(CFG_FORMAT_SOURCE_OBJECTS) $(CFG_FORMAT_ARCHIVES) \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start \
		--export=main --export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		$(CFG_FORMAT_OBJECTS) $(CFG_FORMAT_SOURCE_OBJECTS) $(CFG_FORMAT_ARCHIVES) \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

cfg-format-guest-source: | cfg-format-build-inputs
	@set -eu; for source in $(CFG_FORMAT_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(CFG_FORMAT_SOURCE_RULE)/build.make \
			"$(CFG_FORMAT_SOURCE_RULE)/$$source.cpp.obj"; \
	done

cfg-format-native-source: | module-cache-build-inputs
	for profile in native threaded; do \
		flag=; test "$$profile" != threaded || flag=-DWASTE_TEST_THREADED_BRANCH; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/llvm/lib/Analysis/CFGPrinter.cpp\" \
			-fsyntax-only "$(CFG_FORMAT_DIR)/native.cpp" || exit; \
	done

test-cfg-format: install-libc aux-test-runner cfg-format-fixture
	bash "$(CFG_FORMAT_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(CFG_FORMAT_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(wildcard $(CFG_FORMAT_BUILD)/*.d)
