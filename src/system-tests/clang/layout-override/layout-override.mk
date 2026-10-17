# Actual production record-layout parser and shared file-line input.
LAYOUT_OVERRIDE_DIR := $(REPO_ROOT)/src/system-tests/clang/layout-override
LAYOUT_OVERRIDE_BUILD := $(abspath $(BUILD_DIR))/clang/layout-override
LAYOUT_OVERRIDE_INPUTS := $(LAYOUT_OVERRIDE_DIR)/layout-override.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch $(REPO_ROOT)/src/aux/clang/clang.mk \
	$(wildcard $(REPO_ROOT)/src/aux/clang/*.cmake) \
	$(REPO_ROOT)/src/aux/clang/include/clang/Frontend/WasteFileInput.h
LAYOUT_OVERRIDE_SOURCE_NAMES := LayoutOverrideSource
LAYOUT_OVERRIDE_SOURCE_RULE := tools/clang/lib/Frontend/CMakeFiles/obj.clangFrontend.dir
LAYOUT_OVERRIDE_SOURCE_OBJECTS := $(addprefix $(MODULE_CACHE_GUEST)/$(LAYOUT_OVERRIDE_SOURCE_RULE)/,\
	$(addsuffix .cpp.obj,$(LAYOUT_OVERRIDE_SOURCE_NAMES)))
LAYOUT_OVERRIDE_SOURCE_STAMP := $(LAYOUT_OVERRIDE_BUILD)/.sources-checked
LAYOUT_OVERRIDE_OBJECTS := $(LAYOUT_OVERRIDE_BUILD)/probe.o $(VFS_ROOT)/usr/lib/waste/cc/crt1.o $(LAYOUT_OVERRIDE_SOURCE_OBJECTS)
LAYOUT_OVERRIDE_CLANG_ARCHIVES := $(addprefix $(MODULE_CACHE_GUEST)/lib/libclang,AST.a Lex.a Basic.a)
LAYOUT_OVERRIDE_ARCHIVES := $(addprefix $(MODULE_CACHE_GUEST)/lib/libLLVM,\
	Core.a Remarks.a BitstreamReader.a BinaryFormat.a FrontendOpenMP.a FrontendHLSL.a FrontendDirective.a TargetParser.a Support.a Demangle.a)

.PHONY: layout-override-fixture layout-override-build-inputs layout-override-guest-source layout-override-native-source test-layout-override
layout-override-fixture: $(LAYOUT_OVERRIDE_BUILD)/probe.wasm

$(MODULE_CACHE_GUEST)/lib/libLLVMFrontendDirective.a: | module-cache-build-inputs
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" LLVMFrontendDirective -j1
layout-override-build-inputs: $(LAYOUT_OVERRIDE_SOURCE_STAMP)

$(LAYOUT_OVERRIDE_SOURCE_STAMP): $(LAYOUT_OVERRIDE_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" clangAST -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(LAYOUT_OVERRIDE_SOURCE_RULE)/build.make $(LAYOUT_OVERRIDE_SOURCE_RULE)/depend
	@set -eu; for source in $(LAYOUT_OVERRIDE_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(LAYOUT_OVERRIDE_SOURCE_RULE)/build.make \
			"$(LAYOUT_OVERRIDE_SOURCE_RULE)/$$source.cpp.obj"; \
	done
	touch "$@"

$(LAYOUT_OVERRIDE_SOURCE_OBJECTS): | layout-override-build-inputs
	test -s "$@" || $(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(LAYOUT_OVERRIDE_SOURCE_RULE)/build.make "$(patsubst $(MODULE_CACHE_GUEST)/%,%,$@)"

$(LAYOUT_OVERRIDE_CLANG_ARCHIVES): | layout-override-build-inputs
	test -s "$@"

$(LAYOUT_OVERRIDE_BUILD)/probe.o: $(LAYOUT_OVERRIDE_DIR)/probe.cpp $(LAYOUT_OVERRIDE_INPUTS) | layout-override-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter -fno-access-control -I "$(MODULE_CACHE_SOURCE)/clang/lib/Frontend" -c "$<" -o "$@"

$(LAYOUT_OVERRIDE_BUILD)/probe.wasm: $(LAYOUT_OVERRIDE_OBJECTS) $(LAYOUT_OVERRIDE_SOURCE_STAMP) $(LAYOUT_OVERRIDE_SOURCE_OBJECTS) $(LAYOUT_OVERRIDE_CLANG_ARCHIVES) $(LAYOUT_OVERRIDE_ARCHIVES) \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start \
		--export=main --export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		$(LAYOUT_OVERRIDE_OBJECTS) $(LAYOUT_OVERRIDE_CLANG_ARCHIVES) $(LAYOUT_OVERRIDE_ARCHIVES) \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

layout-override-guest-source: | layout-override-build-inputs
	@set -eu; for source in $(LAYOUT_OVERRIDE_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(LAYOUT_OVERRIDE_SOURCE_RULE)/build.make \
			"$(LAYOUT_OVERRIDE_SOURCE_RULE)/$$source.cpp.obj"; \
	done

layout-override-native-source: | module-cache-build-inputs
	for profile in native native-threads guest-threads; do \
		flag=; case "$$profile" in native-threads) flag=-DWASTE_TEST_NATIVE_THREADS;; guest-threads) flag=-DWASTE_TEST_GUEST_THREADS;; esac; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/clang/include" \
			-isystem "$(MODULE_CACHE_GUEST)/tools/clang/include" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/clang/lib/Frontend/LayoutOverrideSource.cpp\" \
			-fsyntax-only "$(LAYOUT_OVERRIDE_DIR)/native.cpp" || exit; \
	done

test-layout-override: aux-test-runner layout-override-fixture
	bash "$(LAYOUT_OVERRIDE_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(LAYOUT_OVERRIDE_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(wildcard $(LAYOUT_OVERRIDE_BUILD)/*.d)
