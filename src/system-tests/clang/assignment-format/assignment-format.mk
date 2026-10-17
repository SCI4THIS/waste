# Actual private assignment interval formatter; complete production source gate.
ASSIGNMENT_FORMAT_DIR := $(REPO_ROOT)/src/system-tests/clang/assignment-format
ASSIGNMENT_FORMAT_BUILD := $(abspath $(BUILD_DIR))/clang/assignment-format
ASSIGNMENT_FORMAT_INPUTS := $(ASSIGNMENT_FORMAT_DIR)/assignment-format.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch $(REPO_ROOT)/src/aux/clang/clang.mk \
	$(wildcard $(REPO_ROOT)/src/aux/clang/*.cmake)
ASSIGNMENT_FORMAT_SOURCE_NAMES := AssignmentTrackingAnalysis
ASSIGNMENT_FORMAT_SOURCE_RULE := lib/CodeGen/CMakeFiles/LLVMCodeGen.dir
ASSIGNMENT_FORMAT_SOURCE_OBJECTS := $(addprefix $(MODULE_CACHE_GUEST)/$(ASSIGNMENT_FORMAT_SOURCE_RULE)/,\
	$(addsuffix .cpp.obj,$(ASSIGNMENT_FORMAT_SOURCE_NAMES)))
ASSIGNMENT_FORMAT_SOURCE_STAMP := $(ASSIGNMENT_FORMAT_BUILD)/.sources-checked
ASSIGNMENT_FORMAT_OBJECTS := $(ASSIGNMENT_FORMAT_BUILD)/probe.o $(MODULE_CACHE_BUILD)/crt.o
ASSIGNMENT_FORMAT_ARCHIVES := $(addprefix $(MODULE_CACHE_GUEST)/lib/libLLVM,\
	Core.a Remarks.a BitstreamReader.a BinaryFormat.a TargetParser.a Support.a Demangle.a)

.PHONY: assignment-format-fixture assignment-format-build-inputs assignment-format-guest-source assignment-format-native-source test-assignment-format
assignment-format-fixture: $(ASSIGNMENT_FORMAT_BUILD)/probe.wasm
assignment-format-build-inputs: $(ASSIGNMENT_FORMAT_SOURCE_STAMP)

$(ASSIGNMENT_FORMAT_SOURCE_STAMP): $(ASSIGNMENT_FORMAT_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(ASSIGNMENT_FORMAT_SOURCE_RULE)/build.make $(ASSIGNMENT_FORMAT_SOURCE_RULE)/depend
	@set -eu; for source in $(ASSIGNMENT_FORMAT_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(ASSIGNMENT_FORMAT_SOURCE_RULE)/build.make \
			"$(ASSIGNMENT_FORMAT_SOURCE_RULE)/$$source.cpp.obj"; \
	done
	touch "$@"

$(ASSIGNMENT_FORMAT_SOURCE_OBJECTS): | assignment-format-build-inputs
	test -s "$@" || $(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(ASSIGNMENT_FORMAT_SOURCE_RULE)/build.make "$(patsubst $(MODULE_CACHE_GUEST)/%,%,$@)"

$(ASSIGNMENT_FORMAT_BUILD)/probe.o: $(ASSIGNMENT_FORMAT_DIR)/probe.cpp $(ASSIGNMENT_FORMAT_INPUTS) | assignment-format-build-inputs
	mkdir -p "$(@D)"
	# Test-only access to the actual private formatter; production access is unchanged.
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter -fno-access-control -I "$(MODULE_CACHE_SOURCE)/llvm/lib/CodeGen" -c "$<" -o "$@"

$(ASSIGNMENT_FORMAT_BUILD)/probe.wasm: $(ASSIGNMENT_FORMAT_OBJECTS) $(ASSIGNMENT_FORMAT_SOURCE_STAMP) $(ASSIGNMENT_FORMAT_SOURCE_OBJECTS) $(ASSIGNMENT_FORMAT_ARCHIVES) \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start \
		--export=main --export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		$(ASSIGNMENT_FORMAT_OBJECTS) $(ASSIGNMENT_FORMAT_ARCHIVES) \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

assignment-format-guest-source: | assignment-format-build-inputs
	@set -eu; for source in $(ASSIGNMENT_FORMAT_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(ASSIGNMENT_FORMAT_SOURCE_RULE)/build.make \
			"$(ASSIGNMENT_FORMAT_SOURCE_RULE)/$$source.cpp.obj"; \
	done

assignment-format-native-source: | module-cache-build-inputs
	for profile in native native-threads guest-threads; do \
		flag=; case "$$profile" in native-threads) flag=-DWASTE_TEST_NATIVE_THREADS;; guest-threads) flag=-DWASTE_TEST_GUEST_THREADS;; esac; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/llvm/lib/CodeGen/AssignmentTrackingAnalysis.cpp\" \
			-fsyntax-only "$(ASSIGNMENT_FORMAT_DIR)/native.cpp" || exit; \
	done

test-assignment-format: install-libc aux-test-runner assignment-format-fixture
	bash "$(ASSIGNMENT_FORMAT_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(ASSIGNMENT_FORMAT_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(wildcard $(ASSIGNMENT_FORMAT_BUILD)/*.d)
