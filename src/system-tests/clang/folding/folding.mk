# Optional host math folds, exact APFloat folds and target availability.
FOLDING_DIR := $(REPO_ROOT)/src/system-tests/clang/folding
FOLDING_BUILD := $(abspath $(BUILD_DIR))/clang/folding
FOLDING_INPUTS := $(FOLDING_DIR)/folding.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch $(REPO_ROOT)/src/aux/clang/clang.mk \
	$(wildcard $(REPO_ROOT)/src/aux/clang/*.cmake)
FOLDING_SOURCE_NAMES := ConstantFolding TargetLibraryInfo ValueTracking VectorUtils \
	AssumptionCache AssumeBundleQueries GuardUtils FloatingPointPredicateUtils \
	TargetTransformInfo
FOLDING_SOURCE_RULE := lib/Analysis/CMakeFiles/LLVMAnalysis.dir
FOLDING_SOURCE_OBJECTS := $(addprefix $(MODULE_CACHE_GUEST)/$(FOLDING_SOURCE_RULE)/,\
	$(addsuffix .cpp.obj,$(FOLDING_SOURCE_NAMES)))
FOLDING_SOURCE_STAMP := $(FOLDING_BUILD)/.sources-checked
FOLDING_ARCHIVE_STAMP := $(FOLDING_BUILD)/.archive-checked
FOLDING_ANALYSIS := $(MODULE_CACHE_GUEST)/lib/libLLVMAnalysis.a
FOLDING_OBJECTS := $(FOLDING_BUILD)/probe.o $(MODULE_CACHE_BUILD)/crt.o
FOLDING_ARCHIVES := $(addprefix $(MODULE_CACHE_GUEST)/lib/libLLVM,\
	Analysis.a Core.a Remarks.a BitstreamReader.a BinaryFormat.a TargetParser.a Support.a Demangle.a)

.PHONY: folding-fixture folding-build-inputs folding-guest-source folding-native-source test-folding
folding-fixture: $(FOLDING_BUILD)/probe.wasm
folding-build-inputs: $(FOLDING_SOURCE_STAMP)

$(FOLDING_SOURCE_STAMP): $(FOLDING_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(FOLDING_SOURCE_RULE)/build.make $(FOLDING_SOURCE_RULE)/depend
	@set -eu; for source in $(FOLDING_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(FOLDING_SOURCE_RULE)/build.make \
			"$(FOLDING_SOURCE_RULE)/$$source.cpp.obj"; \
	done
	touch "$@"

$(FOLDING_SOURCE_OBJECTS): | folding-build-inputs
	test -s "$@" || $(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(FOLDING_SOURCE_RULE)/build.make "$(patsubst $(MODULE_CACHE_GUEST)/%,%,$@)"

$(FOLDING_ARCHIVE_STAMP): $(FOLDING_SOURCE_STAMP) $(FOLDING_INPUTS)
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" LLVMAnalysis -j1
	touch "$@"

$(FOLDING_ANALYSIS): | $(FOLDING_ARCHIVE_STAMP)
	test -s "$@" || $(MAKE) -C "$(MODULE_CACHE_GUEST)" LLVMAnalysis -j1

$(FOLDING_BUILD)/probe.o: $(FOLDING_DIR)/probe.cpp $(FOLDING_INPUTS) | folding-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter -c "$<" -o "$@"

$(FOLDING_BUILD)/probe.wasm: $(FOLDING_OBJECTS) $(FOLDING_SOURCE_STAMP) $(FOLDING_ARCHIVE_STAMP) $(FOLDING_SOURCE_OBJECTS) $(FOLDING_ARCHIVES) \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start \
		--export=main --export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		$(FOLDING_OBJECTS) $(FOLDING_ARCHIVES) \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

folding-guest-source: | folding-build-inputs
	@set -eu; for source in $(FOLDING_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(FOLDING_SOURCE_RULE)/build.make \
			"$(FOLDING_SOURCE_RULE)/$$source.cpp.obj"; \
	done

folding-native-source: | module-cache-build-inputs
	for profile in native native-threads guest-threads; do \
		flag=; case "$$profile" in native-threads) flag=-DWASTE_TEST_NATIVE_THREADS;; guest-threads) flag=-DWASTE_TEST_GUEST_THREADS;; esac; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" -isystem "$(MODULE_CACHE_GUEST)/include" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/llvm/lib/Analysis/ConstantFolding.cpp\" \
			-fsyntax-only "$(FOLDING_DIR)/native.cpp" || exit; \
	done

test-folding: install-libc aux-test-runner folding-fixture
	bash "$(FOLDING_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(FOLDING_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(wildcard $(FOLDING_BUILD)/*.d)
