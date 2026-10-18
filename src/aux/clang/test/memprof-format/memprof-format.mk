# Actual private MemProf graph identifier formatter; complete production source gate.
MEMPROF_FORMAT_DIR := $(REPO_ROOT)/src/aux/clang/test/memprof-format
MEMPROF_FORMAT_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/memprof-format
MEMPROF_FORMAT_INPUTS := $(MEMPROF_FORMAT_DIR)/memprof-format.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch $(REPO_ROOT)/src/aux/clang/clang.mk \
	$(wildcard $(REPO_ROOT)/src/aux/clang/*.cmake)
MEMPROF_FORMAT_SOURCE_NAMES := MemProfContextDisambiguation
MEMPROF_FORMAT_SOURCE_RULE := lib/Transforms/IPO/CMakeFiles/LLVMipo.dir
MEMPROF_FORMAT_SOURCE_OBJECTS := $(addprefix $(MODULE_CACHE_GUEST)/$(MEMPROF_FORMAT_SOURCE_RULE)/,\
	$(addsuffix .cpp.obj,$(MEMPROF_FORMAT_SOURCE_NAMES)))
MEMPROF_FORMAT_SOURCE_STAMP := $(MEMPROF_FORMAT_BUILD)/.sources-checked
MEMPROF_FORMAT_OBJECTS := $(MEMPROF_FORMAT_BUILD)/probe.o $(MODULE_CACHE_BUILD)/crt.o
MEMPROF_FORMAT_ARCHIVES := $(addprefix $(MODULE_CACHE_GUEST)/lib/libLLVM,\
	Core.a Remarks.a BitstreamReader.a BinaryFormat.a TargetParser.a Support.a Demangle.a)

.PHONY: memprof-format-fixture memprof-format-build-inputs memprof-format-guest-source memprof-format-native-source test-memprof-format
memprof-format-fixture: $(MEMPROF_FORMAT_BUILD)/probe.wasm
memprof-format-build-inputs: $(MEMPROF_FORMAT_SOURCE_STAMP)

$(MEMPROF_FORMAT_SOURCE_STAMP): $(MEMPROF_FORMAT_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(MEMPROF_FORMAT_SOURCE_RULE)/build.make $(MEMPROF_FORMAT_SOURCE_RULE)/depend
	@set -eu; for source in $(MEMPROF_FORMAT_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(MEMPROF_FORMAT_SOURCE_RULE)/build.make \
			"$(MEMPROF_FORMAT_SOURCE_RULE)/$$source.cpp.obj"; \
	done
	touch "$@"

$(MEMPROF_FORMAT_SOURCE_OBJECTS): | memprof-format-build-inputs
	test -s "$@" || $(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(MEMPROF_FORMAT_SOURCE_RULE)/build.make "$(patsubst $(MODULE_CACHE_GUEST)/%,%,$@)"

$(MEMPROF_FORMAT_BUILD)/probe.o: $(MEMPROF_FORMAT_DIR)/probe.cpp $(MEMPROF_FORMAT_INPUTS) | memprof-format-build-inputs
	mkdir -p "$(@D)"
	# Test-only access to the actual private formatter; production access is unchanged.
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter -fno-access-control -I "$(MODULE_CACHE_SOURCE)/llvm/lib/Transforms/IPO" -c "$<" -o "$@"

$(MEMPROF_FORMAT_BUILD)/probe.wasm: $(MEMPROF_FORMAT_OBJECTS) $(MEMPROF_FORMAT_SOURCE_STAMP) $(MEMPROF_FORMAT_SOURCE_OBJECTS) $(MEMPROF_FORMAT_ARCHIVES) \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start \
		--export=main --export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		$(MEMPROF_FORMAT_OBJECTS) $(MEMPROF_FORMAT_ARCHIVES) \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

memprof-format-guest-source: | memprof-format-build-inputs
	@set -eu; for source in $(MEMPROF_FORMAT_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(MEMPROF_FORMAT_SOURCE_RULE)/build.make \
			"$(MEMPROF_FORMAT_SOURCE_RULE)/$$source.cpp.obj"; \
	done

memprof-format-native-source: | module-cache-build-inputs
	for profile in native native-threads guest-threads; do \
		flag=; case "$$profile" in native-threads) flag=-DWASTE_TEST_NATIVE_THREADS;; guest-threads) flag=-DWASTE_TEST_GUEST_THREADS;; esac; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/llvm/lib/Transforms/IPO/MemProfContextDisambiguation.cpp\" \
			-fsyntax-only "$(MEMPROF_FORMAT_DIR)/native.cpp" || exit; \
	done

test-memprof-format: install-libc aux-test-runner memprof-format-fixture
	bash "$(MEMPROF_FORMAT_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(MEMPROF_FORMAT_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(wildcard $(MEMPROF_FORMAT_BUILD)/*.d)
