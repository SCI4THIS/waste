# Actual shadow callback declarations; complete production source gate.
SHADOW_CALLBACKS_DIR := $(REPO_ROOT)/src/aux/clang/test/shadow-callbacks
SHADOW_CALLBACKS_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/shadow-callbacks
SHADOW_CALLBACKS_INPUTS := $(SHADOW_CALLBACKS_DIR)/shadow-callbacks.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch $(REPO_ROOT)/src/aux/clang/clang.mk \
	$(wildcard $(REPO_ROOT)/src/aux/clang/*.cmake)
SHADOW_CALLBACKS_SOURCE_NAMES := AddressSanitizer
SHADOW_CALLBACKS_SOURCE_RULE := lib/Transforms/Instrumentation/CMakeFiles/LLVMInstrumentation.dir
SHADOW_CALLBACKS_SOURCE_OBJECTS := $(addprefix $(MODULE_CACHE_GUEST)/$(SHADOW_CALLBACKS_SOURCE_RULE)/,\
	$(addsuffix .cpp.obj,$(SHADOW_CALLBACKS_SOURCE_NAMES)))
SHADOW_CALLBACKS_SOURCE_STAMP := $(SHADOW_CALLBACKS_BUILD)/.sources-checked
SHADOW_CALLBACKS_OBJECTS := $(SHADOW_CALLBACKS_BUILD)/probe.o $(MODULE_CACHE_BUILD)/crt.o
SHADOW_CALLBACKS_ARCHIVES := $(addprefix $(MODULE_CACHE_GUEST)/lib/libLLVM,\
	TransformUtils.a Analysis.a Core.a Remarks.a BitstreamReader.a BinaryFormat.a TargetParser.a Support.a Demangle.a)

.PHONY: shadow-callbacks-fixture shadow-callbacks-build-inputs shadow-callbacks-guest-source shadow-callbacks-native-source test-shadow-callbacks
shadow-callbacks-fixture: $(SHADOW_CALLBACKS_BUILD)/probe.wasm
shadow-callbacks-build-inputs: $(SHADOW_CALLBACKS_SOURCE_STAMP)

$(SHADOW_CALLBACKS_SOURCE_STAMP): $(SHADOW_CALLBACKS_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(SHADOW_CALLBACKS_SOURCE_RULE)/build.make $(SHADOW_CALLBACKS_SOURCE_RULE)/depend
	@set -eu; for source in $(SHADOW_CALLBACKS_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(SHADOW_CALLBACKS_SOURCE_RULE)/build.make \
			"$(SHADOW_CALLBACKS_SOURCE_RULE)/$$source.cpp.obj"; \
	done
	touch "$@"

$(SHADOW_CALLBACKS_SOURCE_OBJECTS): | shadow-callbacks-build-inputs
	test -s "$@" || $(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(SHADOW_CALLBACKS_SOURCE_RULE)/build.make "$(patsubst $(MODULE_CACHE_GUEST)/%,%,$@)"

$(SHADOW_CALLBACKS_BUILD)/probe.o: $(SHADOW_CALLBACKS_DIR)/probe.cpp $(SHADOW_CALLBACKS_INPUTS) | shadow-callbacks-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter -I "$(MODULE_CACHE_SOURCE)/llvm/lib/Transforms/Instrumentation" -c "$<" -o "$@"

$(SHADOW_CALLBACKS_BUILD)/probe.wasm: $(SHADOW_CALLBACKS_OBJECTS) $(SHADOW_CALLBACKS_SOURCE_STAMP) $(SHADOW_CALLBACKS_SOURCE_OBJECTS) $(SHADOW_CALLBACKS_ARCHIVES) \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start \
		--export=main --export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		$(SHADOW_CALLBACKS_OBJECTS) $(SHADOW_CALLBACKS_ARCHIVES) \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

shadow-callbacks-guest-source: | shadow-callbacks-build-inputs
	@set -eu; for source in $(SHADOW_CALLBACKS_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(SHADOW_CALLBACKS_SOURCE_RULE)/build.make \
			"$(SHADOW_CALLBACKS_SOURCE_RULE)/$$source.cpp.obj"; \
	done

shadow-callbacks-native-source: | module-cache-build-inputs
	for profile in native native-threads guest-threads; do \
		flag=; case "$$profile" in native-threads) flag=-DWASTE_TEST_NATIVE_THREADS;; guest-threads) flag=-DWASTE_TEST_GUEST_THREADS;; esac; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/llvm/lib/Transforms/Instrumentation/AddressSanitizer.cpp\" \
			-fsyntax-only "$(SHADOW_CALLBACKS_DIR)/native.cpp" || exit; \
	done

test-shadow-callbacks: install-libc aux-test-runner shadow-callbacks-fixture
	bash "$(SHADOW_CALLBACKS_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(SHADOW_CALLBACKS_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(wildcard $(SHADOW_CALLBACKS_BUILD)/*.d)
