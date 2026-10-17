# Actual inlining statistics and compiled mixed floating variadic arguments.
INLINING_DIR := $(REPO_ROOT)/src/system-tests/clang/inlining
INLINING_BUILD := $(abspath $(BUILD_DIR))/clang/inlining
INLINING_INPUTS := $(INLINING_DIR)/inlining.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch $(REPO_ROOT)/src/aux/clang/clang.mk \
	$(wildcard $(REPO_ROOT)/src/aux/clang/*.cmake)
INLINING_SOURCE_NAMES := ImportedFunctionsInliningStatistics
INLINING_SOURCE_RULE := lib/Analysis/CMakeFiles/LLVMAnalysis.dir
INLINING_SOURCE_OBJECTS := $(addprefix $(MODULE_CACHE_GUEST)/$(INLINING_SOURCE_RULE)/,\
	$(addsuffix .cpp.obj,$(INLINING_SOURCE_NAMES)))
INLINING_SOURCE_STAMP := $(INLINING_BUILD)/.sources-checked
INLINING_OBJECTS := $(INLINING_BUILD)/probe.o $(MODULE_CACHE_BUILD)/crt.o
INLINING_ARCHIVES := $(addprefix $(MODULE_CACHE_GUEST)/lib/libLLVM,\
	Core.a Remarks.a BitstreamReader.a BinaryFormat.a TargetParser.a Support.a Demangle.a)

.PHONY: inlining-fixture inlining-build-inputs inlining-guest-source inlining-native-source test-inlining
inlining-fixture: $(INLINING_BUILD)/probe.wasm
inlining-build-inputs: $(INLINING_SOURCE_STAMP)

$(INLINING_SOURCE_STAMP): $(INLINING_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(INLINING_SOURCE_RULE)/build.make $(INLINING_SOURCE_RULE)/depend
	@set -eu; for source in $(INLINING_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(INLINING_SOURCE_RULE)/build.make \
			"$(INLINING_SOURCE_RULE)/$$source.cpp.obj"; \
	done
	touch "$@"

$(INLINING_SOURCE_OBJECTS): | inlining-build-inputs
	test -s "$@" || $(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(INLINING_SOURCE_RULE)/build.make "$(patsubst $(MODULE_CACHE_GUEST)/%,%,$@)"

$(INLINING_BUILD)/probe.o: $(INLINING_DIR)/probe.cpp $(INLINING_INPUTS) | inlining-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter -I "$(MODULE_CACHE_SOURCE)/llvm/lib/Analysis" -c "$<" -o "$@"

$(INLINING_BUILD)/probe.wasm: $(INLINING_OBJECTS) $(INLINING_SOURCE_STAMP) $(INLINING_SOURCE_OBJECTS) $(INLINING_ARCHIVES) \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start \
		--export=main --export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		$(INLINING_OBJECTS) $(INLINING_ARCHIVES) \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

inlining-guest-source: | inlining-build-inputs
	@set -eu; for source in $(INLINING_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(INLINING_SOURCE_RULE)/build.make \
			"$(INLINING_SOURCE_RULE)/$$source.cpp.obj"; \
	done

inlining-native-source: | module-cache-build-inputs
	for profile in native threaded; do \
		flag=; test "$$profile" != threaded || flag=-DWASTE_TEST_THREADED_BRANCH; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/llvm/lib/Analysis/ImportedFunctionsInliningStatistics.cpp\" \
			-fsyntax-only "$(INLINING_DIR)/native.cpp" || exit; \
	done

test-inlining: install-libc aux-test-runner inlining-fixture
	bash "$(INLINING_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(INLINING_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(wildcard $(INLINING_BUILD)/*.d)
