# Actual MC/DC coverage formatting; complete production source gate.
COVERAGE_FORMAT_DIR := $(REPO_ROOT)/src/aux/clang/test/coverage-format
COVERAGE_FORMAT_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/coverage-format
COVERAGE_FORMAT_INPUTS := $(COVERAGE_FORMAT_DIR)/coverage-format.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch $(REPO_ROOT)/src/aux/clang/clang.mk \
	$(wildcard $(REPO_ROOT)/src/aux/clang/*.cmake)
COVERAGE_FORMAT_SOURCE_NAMES := CoverageMapping
COVERAGE_FORMAT_SOURCE_RULE := lib/ProfileData/Coverage/CMakeFiles/LLVMCoverage.dir
COVERAGE_FORMAT_SOURCE_OBJECTS := $(addprefix $(MODULE_CACHE_GUEST)/$(COVERAGE_FORMAT_SOURCE_RULE)/,\
	$(addsuffix .cpp.obj,$(COVERAGE_FORMAT_SOURCE_NAMES)))
COVERAGE_FORMAT_SOURCE_STAMP := $(COVERAGE_FORMAT_BUILD)/.sources-checked
COVERAGE_FORMAT_OBJECTS := $(COVERAGE_FORMAT_BUILD)/probe.o $(VFS_ROOT)/usr/lib/waste/cc/crt1.o $(COVERAGE_FORMAT_SOURCE_OBJECTS)
COVERAGE_FORMAT_ARCHIVES := $(addprefix $(MODULE_CACHE_GUEST)/lib/libLLVM,\
	Support.a Demangle.a)

.PHONY: coverage-format-fixture coverage-format-build-inputs coverage-format-guest-source coverage-format-native-source test-coverage-format
coverage-format-fixture: $(COVERAGE_FORMAT_BUILD)/probe.wasm
coverage-format-build-inputs: $(COVERAGE_FORMAT_SOURCE_STAMP)

$(COVERAGE_FORMAT_SOURCE_STAMP): $(COVERAGE_FORMAT_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(COVERAGE_FORMAT_SOURCE_RULE)/build.make $(COVERAGE_FORMAT_SOURCE_RULE)/depend
	@set -eu; for source in $(COVERAGE_FORMAT_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(COVERAGE_FORMAT_SOURCE_RULE)/build.make \
			"$(COVERAGE_FORMAT_SOURCE_RULE)/$$source.cpp.obj"; \
	done
	touch "$@"

$(COVERAGE_FORMAT_SOURCE_OBJECTS): | coverage-format-build-inputs
	test -s "$@" || $(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(COVERAGE_FORMAT_SOURCE_RULE)/build.make "$(patsubst $(MODULE_CACHE_GUEST)/%,%,$@)"

$(COVERAGE_FORMAT_BUILD)/probe.o: $(COVERAGE_FORMAT_DIR)/probe.cpp $(COVERAGE_FORMAT_INPUTS) | coverage-format-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter -I "$(MODULE_CACHE_SOURCE)/llvm/lib/ProfileData/Coverage" -c "$<" -o "$@"

$(COVERAGE_FORMAT_BUILD)/probe.wasm: $(COVERAGE_FORMAT_OBJECTS) $(COVERAGE_FORMAT_SOURCE_STAMP) $(COVERAGE_FORMAT_SOURCE_OBJECTS) $(COVERAGE_FORMAT_ARCHIVES) \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start \
		--export=main --export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		$(COVERAGE_FORMAT_OBJECTS) $(COVERAGE_FORMAT_ARCHIVES) \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

coverage-format-guest-source: | coverage-format-build-inputs
	@set -eu; for source in $(COVERAGE_FORMAT_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(COVERAGE_FORMAT_SOURCE_RULE)/build.make \
			"$(COVERAGE_FORMAT_SOURCE_RULE)/$$source.cpp.obj"; \
	done

coverage-format-native-source: | module-cache-build-inputs
	for profile in native native-threads guest-threads; do \
		flag=; case "$$profile" in native-threads) flag=-DWASTE_TEST_NATIVE_THREADS;; guest-threads) flag=-DWASTE_TEST_GUEST_THREADS;; esac; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/llvm/lib/ProfileData/Coverage/CoverageMapping.cpp\" \
			-fsyntax-only "$(COVERAGE_FORMAT_DIR)/native.cpp" || exit; \
	done

test-coverage-format: aux-test-runner coverage-format-fixture
	bash "$(COVERAGE_FORMAT_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(COVERAGE_FORMAT_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(wildcard $(COVERAGE_FORMAT_BUILD)/*.d)
