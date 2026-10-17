# Actual HLSL driver extension validator, using existing LLVM regex in the guest.
SPIRV_NAME_DIR := $(REPO_ROOT)/src/system-tests/clang/spirv-name
SPIRV_NAME_BUILD := $(abspath $(BUILD_DIR))/clang/spirv-name
SPIRV_NAME_SOURCE := $(MODULE_CACHE_SOURCE)/clang/lib/Driver/ToolChains/HLSL.cpp
SPIRV_NAME_RULE := tools/clang/lib/Driver/CMakeFiles/obj.clangDriver.dir
SPIRV_NAME_INPUTS := $(SPIRV_NAME_DIR)/spirv-name.mk $(SPIRV_NAME_DIR)/cases.h $(SPIRV_NAME_DIR)/observations.h \
	$(SDK_HEADERS) $(REPO_ROOT)/src/aux/clang/clang-waste.patch
SPIRV_NAME_NATIVE_FLAGS := -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter -fno-exceptions -fno-rtti \
	-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" -isystem "$(MODULE_CACHE_GUEST)/include" \
	-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" -isystem "$(MODULE_CACHE_SOURCE)/clang/include" \
	-isystem "$(MODULE_CACHE_GUEST)/tools/clang/include" -DWASTE_CHECK_SOURCE=\"$(SPIRV_NAME_SOURCE)\"

SPIRV_NAME_NATIVE_LIBRARIES := $(MODULE_CACHE_CLANG_BUILD)/native/lib/libLLVMSupport.a $(MODULE_CACHE_CLANG_BUILD)/native/lib/libLLVMDemangle.a
$(SPIRV_NAME_NATIVE_LIBRARIES): | module-cache-build-inputs
	test -s "$@"

.PHONY: spirv-name-fixture spirv-name-source test-spirv-name
spirv-name-fixture: $(SPIRV_NAME_BUILD)/probe.wasm $(SPIRV_NAME_BUILD)/reference.bin

$(SPIRV_NAME_BUILD)/probe.o: $(SPIRV_NAME_DIR)/probe.cpp $(SPIRV_NAME_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter \
		-DWASTE_CHECK_SOURCE=\"$(SPIRV_NAME_SOURCE)\" -c "$<" -o "$@"

$(SPIRV_NAME_BUILD)/probe.wasm: $(SPIRV_NAME_BUILD)/probe.o $(VFS_ROOT)/usr/lib/waste/cc/crt1.o \
	$(MODULE_CACHE_SUPPORT) $(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__stack_pointer --export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(VFS_ROOT)/usr/lib/waste/cc/crt1.o" "$(SPIRV_NAME_BUILD)/probe.o" "$(MODULE_CACHE_SUPPORT)" \
		"$(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a" "$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

$(SPIRV_NAME_BUILD)/reference: $(SPIRV_NAME_DIR)/reference.cpp $(SPIRV_NAME_INPUTS) $(SPIRV_NAME_NATIVE_LIBRARIES) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(SPIRV_NAME_NATIVE_FLAGS) -MMD -MP -MF "$(SPIRV_NAME_BUILD)/reference.d" -ffunction-sections -fdata-sections \
		-Wl,--gc-sections "$<" $(SPIRV_NAME_NATIVE_LIBRARIES) -o "$@"

$(SPIRV_NAME_BUILD)/reference.bin: $(SPIRV_NAME_BUILD)/reference
	"$<" > "$@"

spirv-name-source: | module-cache-build-inputs
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(SPIRV_NAME_RULE)/build.make $(SPIRV_NAME_RULE)/depend
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(SPIRV_NAME_RULE)/build.make $(SPIRV_NAME_RULE)/ToolChains/HLSL.cpp.obj
	@set -eu; for profile in native native-threads guest-threads; do \
		flag=; case "$$profile" in native-threads) flag=-DWASTE_TEST_NATIVE_THREADS;; guest-threads) flag=-DWASTE_TEST_GUEST_THREADS;; esac; \
		$(MODULE_CACHE_CXX) $(SPIRV_NAME_NATIVE_FLAGS) $$flag -fsyntax-only "$(SPIRV_NAME_DIR)/native.cpp"; \
	done

test-spirv-name: spirv-name-fixture aux-test-runner
	bash "$(SPIRV_NAME_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(SPIRV_NAME_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(wildcard $(SPIRV_NAME_BUILD)/*.d)
