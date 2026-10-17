# Required ExtTSP layout and scoped exclusion of unused guest CDSort.
CODE_LAYOUT_DIR := $(REPO_ROOT)/src/system-tests/clang/code-layout
CODE_LAYOUT_BUILD := $(abspath $(BUILD_DIR))/clang/code-layout
CODE_LAYOUT_INPUTS := $(CODE_LAYOUT_DIR)/code-layout.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch $(REPO_ROOT)/src/aux/clang/clang.mk \
	$(wildcard $(REPO_ROOT)/src/aux/clang/*.cmake)
CODE_LAYOUT_SOURCE_NAMES := CodeLayout
CODE_LAYOUT_SOURCE_RULE := lib/Transforms/Utils/CMakeFiles/LLVMTransformUtils.dir
CODE_LAYOUT_SOURCE_OBJECTS := $(addprefix $(MODULE_CACHE_GUEST)/$(CODE_LAYOUT_SOURCE_RULE)/,\
	$(addsuffix .cpp.obj,$(CODE_LAYOUT_SOURCE_NAMES)))
CODE_LAYOUT_SOURCE_STAMP := $(CODE_LAYOUT_BUILD)/.sources-checked
CODE_LAYOUT_OBJECTS := $(CODE_LAYOUT_BUILD)/probe.o $(MODULE_CACHE_BUILD)/crt.o
CODE_LAYOUT_ARCHIVES := $(addprefix $(MODULE_CACHE_GUEST)/lib/libLLVM,\
	Support.a Demangle.a)

.PHONY: code-layout-fixture code-layout-build-inputs code-layout-guest-source code-layout-native-source test-code-layout
code-layout-fixture: $(CODE_LAYOUT_BUILD)/probe.wasm
code-layout-build-inputs: $(CODE_LAYOUT_SOURCE_STAMP)

$(CODE_LAYOUT_SOURCE_STAMP): $(CODE_LAYOUT_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(CODE_LAYOUT_SOURCE_RULE)/build.make $(CODE_LAYOUT_SOURCE_RULE)/depend
	@set -eu; for source in $(CODE_LAYOUT_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(CODE_LAYOUT_SOURCE_RULE)/build.make \
			"$(CODE_LAYOUT_SOURCE_RULE)/$$source.cpp.obj"; \
	done
	touch "$@"

$(CODE_LAYOUT_SOURCE_OBJECTS): | code-layout-build-inputs
	test -s "$@" || $(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(CODE_LAYOUT_SOURCE_RULE)/build.make "$(patsubst $(MODULE_CACHE_GUEST)/%,%,$@)"

$(CODE_LAYOUT_BUILD)/probe.o: $(CODE_LAYOUT_DIR)/probe.cpp $(CODE_LAYOUT_INPUTS) | code-layout-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter -c "$<" -o "$@"

$(CODE_LAYOUT_BUILD)/probe.wasm: $(CODE_LAYOUT_OBJECTS) $(CODE_LAYOUT_SOURCE_STAMP) $(CODE_LAYOUT_SOURCE_OBJECTS) $(CODE_LAYOUT_ARCHIVES) \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start \
		--export=main --export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		$(CODE_LAYOUT_OBJECTS) $(CODE_LAYOUT_SOURCE_OBJECTS) $(CODE_LAYOUT_ARCHIVES) \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

code-layout-guest-source: | code-layout-build-inputs
	@set -eu; for source in $(CODE_LAYOUT_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(CODE_LAYOUT_SOURCE_RULE)/build.make \
			"$(CODE_LAYOUT_SOURCE_RULE)/$$source.cpp.obj"; \
	done

code-layout-native-source: | module-cache-build-inputs
	for profile in native native-threads guest-threads; do \
		flag=; case "$$profile" in native-threads) flag=-DWASTE_TEST_NATIVE_THREADS;; guest-threads) flag=-DWASTE_TEST_GUEST_THREADS;; esac; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" -isystem "$(MODULE_CACHE_GUEST)/include" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/llvm/lib/Transforms/Utils/CodeLayout.cpp\" \
			-fsyntax-only "$(CODE_LAYOUT_DIR)/native.cpp" || exit; \
	done

test-code-layout: install-libc aux-test-runner code-layout-fixture
	bash "$(CODE_LAYOUT_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(CODE_LAYOUT_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(wildcard $(CODE_LAYOUT_BUILD)/*.d)
