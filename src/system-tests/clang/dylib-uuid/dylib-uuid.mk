# Actual Mach-O UUID parsing/formatting; complete production source gate.
DYLIB_UUID_DIR := $(REPO_ROOT)/src/system-tests/clang/dylib-uuid
DYLIB_UUID_BUILD := $(abspath $(BUILD_DIR))/clang/dylib-uuid
DYLIB_UUID_INPUTS := $(DYLIB_UUID_DIR)/dylib-uuid.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch $(REPO_ROOT)/src/aux/clang/clang.mk \
	$(wildcard $(REPO_ROOT)/src/aux/clang/*.cmake)
DYLIB_UUID_SOURCE_NAMES := DylibReader
DYLIB_UUID_SOURCE_RULE := lib/TextAPI/BinaryReader/CMakeFiles/LLVMTextAPIBinaryReader.dir
DYLIB_UUID_SOURCE_OBJECTS := $(addprefix $(MODULE_CACHE_GUEST)/$(DYLIB_UUID_SOURCE_RULE)/,\
	$(addsuffix .cpp.obj,$(DYLIB_UUID_SOURCE_NAMES)))
DYLIB_UUID_SOURCE_STAMP := $(DYLIB_UUID_BUILD)/.sources-checked
DYLIB_UUID_OBJECTS := $(DYLIB_UUID_BUILD)/probe.o $(VFS_ROOT)/usr/lib/waste/cc/crt1.o
DYLIB_UUID_ARCHIVES := $(addprefix $(MODULE_CACHE_GUEST)/lib/libLLVM,\
	TextAPI.a Object.a MCParser.a MC.a IRReader.a AsmParser.a BitReader.a Core.a TextAPI.a FrontendHLSL.a Remarks.a BitstreamReader.a BinaryFormat.a TargetParser.a Support.a Demangle.a)

.PHONY: dylib-uuid-fixture dylib-uuid-build-inputs dylib-uuid-guest-source dylib-uuid-native-source test-dylib-uuid
dylib-uuid-fixture: $(DYLIB_UUID_BUILD)/probe.wasm
dylib-uuid-build-inputs: $(DYLIB_UUID_SOURCE_STAMP)

$(DYLIB_UUID_SOURCE_STAMP): $(DYLIB_UUID_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(DYLIB_UUID_SOURCE_RULE)/build.make $(DYLIB_UUID_SOURCE_RULE)/depend
	@set -eu; for source in $(DYLIB_UUID_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(DYLIB_UUID_SOURCE_RULE)/build.make \
			"$(DYLIB_UUID_SOURCE_RULE)/$$source.cpp.obj"; \
	done
	touch "$@"

$(DYLIB_UUID_SOURCE_OBJECTS): | dylib-uuid-build-inputs
	test -s "$@" || $(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(DYLIB_UUID_SOURCE_RULE)/build.make "$(patsubst $(MODULE_CACHE_GUEST)/%,%,$@)"

$(DYLIB_UUID_BUILD)/probe.o: $(DYLIB_UUID_DIR)/probe.cpp $(DYLIB_UUID_INPUTS) | dylib-uuid-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter -I "$(MODULE_CACHE_SOURCE)/llvm/lib/TextAPI/BinaryReader" -c "$<" -o "$@"

$(DYLIB_UUID_BUILD)/probe.wasm: $(DYLIB_UUID_OBJECTS) $(DYLIB_UUID_SOURCE_STAMP) $(DYLIB_UUID_SOURCE_OBJECTS) $(DYLIB_UUID_ARCHIVES) \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start \
		--export=main --export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		$(DYLIB_UUID_OBJECTS) $(DYLIB_UUID_ARCHIVES) \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

dylib-uuid-guest-source: | dylib-uuid-build-inputs
	@set -eu; for source in $(DYLIB_UUID_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(DYLIB_UUID_SOURCE_RULE)/build.make \
			"$(DYLIB_UUID_SOURCE_RULE)/$$source.cpp.obj"; \
	done

dylib-uuid-native-source: | module-cache-build-inputs
	for profile in native native-threads guest-threads; do \
		flag=; case "$$profile" in native-threads) flag=-DWASTE_TEST_NATIVE_THREADS;; guest-threads) flag=-DWASTE_TEST_GUEST_THREADS;; esac; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/llvm/lib/TextAPI/BinaryReader/DylibReader.cpp\" \
			-fsyntax-only "$(DYLIB_UUID_DIR)/native.cpp" || exit; \
	done

test-dylib-uuid: aux-test-runner dylib-uuid-fixture
	bash "$(DYLIB_UUID_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(DYLIB_UUID_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(wildcard $(DYLIB_UUID_BUILD)/*.d)
