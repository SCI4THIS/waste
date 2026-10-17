# Actual LLVM TargetParser guest host identity and isolated archive boundary.
TARGET_HOST_DIR := $(REPO_ROOT)/src/system-tests/clang/target-host
TARGET_HOST_BUILD := $(abspath $(BUILD_DIR))/clang/target-host
TARGET_HOST_ARCHIVE := $(MODULE_CACHE_GUEST)/lib/libLLVMTargetParser.a
TARGET_HOST_ARCHIVE_STAMP := $(TARGET_HOST_BUILD)/.archive-checked
TARGET_HOST_INPUTS := $(TARGET_HOST_DIR)/target-host.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch \
	$(REPO_ROOT)/src/aux/clang/clang.mk \
	$(REPO_ROOT)/src/aux/clang/llvm.cmake \
	$(REPO_ROOT)/src/aux/clang/guest.cmake \
	$(REPO_ROOT)/src/aux/clang/guest-toolchain.cmake

.PHONY: target-host-fixture target-host-native-source test-target-host
target-host-fixture: $(TARGET_HOST_BUILD)/probe.wasm

$(TARGET_HOST_ARCHIVE_STAMP): $(TARGET_HOST_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" LLVMTargetParser -j1
	test -s "$(TARGET_HOST_ARCHIVE)"
	touch "$@"

$(TARGET_HOST_ARCHIVE): | $(TARGET_HOST_ARCHIVE_STAMP)
	test -s "$@" || $(MAKE) -C "$(MODULE_CACHE_GUEST)" LLVMTargetParser -j1

$(TARGET_HOST_BUILD)/probe.o: $(TARGET_HOST_DIR)/probe.cpp $(TARGET_HOST_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter -c "$<" -o "$@"

$(TARGET_HOST_BUILD)/probe.wasm: $(TARGET_HOST_BUILD)/probe.o $(MODULE_CACHE_BUILD)/crt.o \
	$(TARGET_HOST_ARCHIVE_STAMP) $(TARGET_HOST_ARCHIVE) $(MODULE_CACHE_SUPPORT) $(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py \
	$(TARGET_HOST_DIR)/target-host.mk
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start \
		--export=main --export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		"$(TARGET_HOST_BUILD)/probe.o" "$(MODULE_CACHE_BUILD)/crt.o" \
		"$(TARGET_HOST_ARCHIVE)" "$(MODULE_CACHE_SUPPORT)" \
		"$(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

target-host-native-source: | module-cache-build-inputs
	$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
		-fno-exceptions -fno-rtti \
		-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
		-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" \
		-fsyntax-only "$(MODULE_CACHE_SOURCE)/llvm/lib/TargetParser/Host.cpp"

test-target-host: install-libc target-host-fixture
	$(MAKE) -C ../cli-rt guest-session guest-session-sanitize
	$(PYTHON) "$(TARGET_HOST_DIR)/native-session.py" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(TARGET_HOST_BUILD)" "$(MODULE_CACHE_SESSION_RUNNER)"

-include $(TARGET_HOST_BUILD)/probe.d
