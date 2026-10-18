# Actual LLVM managed static registry with the guest single-thread profile.
MANAGED_STATIC_DIR := $(REPO_ROOT)/src/aux/clang/test/managed-static
MANAGED_STATIC_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/managed-static
MANAGED_STATIC_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
MANAGED_STATIC_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
MANAGED_STATIC_CC ?= clang
MANAGED_STATIC_CXX ?= clang++
MANAGED_STATIC_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
MANAGED_STATIC_CXXFLAGS := -isystem "$(MANAGED_STATIC_CLANG_BUILD)/cxx/include/c++/v1" \
	$(MANAGED_STATIC_CFLAGS) -isystem "$(MANAGED_STATIC_CLANG_DIR)/include" \
	-isystem "$(MANAGED_STATIC_CLANG_BUILD)/guest/include" \
	-isystem "$(MANAGED_STATIC_CLANG_BUILD)/source/llvm/include" \
	-isystem "$(MANAGED_STATIC_CLANG_BUILD)/source/clang/include" \
	-isystem "$(MANAGED_STATIC_CLANG_BUILD)/guest/tools/clang/include" \
	-std=c++17 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -MD -MP

.PHONY: managed-static-fixture managed-static-build-inputs managed-static-native-source test-managed-static
managed-static-fixture: $(MANAGED_STATIC_BUILD)/probe.wasm
managed-static-build-inputs:
	$(MAKE) -C ../aux "$(MANAGED_STATIC_CLANG_BUILD)/guest/.configured"

$(MANAGED_STATIC_BUILD)/probe.o: $(MANAGED_STATIC_DIR)/probe.cpp $(MANAGED_STATIC_DIR)/managed-static.mk $(SDK_HEADERS) $(MANAGED_STATIC_CLANG_DIR)/clang-waste.patch | managed-static-build-inputs
	mkdir -p "$(@D)"
	$(MANAGED_STATIC_CXX) $(MANAGED_STATIC_CXXFLAGS) -c "$<" -o "$@"

# Compile the actual registry; no constructor, deleter or provider is mocked.
$(MANAGED_STATIC_BUILD)/managed-static.o: $(MANAGED_STATIC_DIR)/managed-static.mk $(SDK_HEADERS) $(MANAGED_STATIC_CLANG_DIR)/clang-waste.patch | managed-static-build-inputs
	mkdir -p "$(@D)"
	$(MANAGED_STATIC_CXX) $(MANAGED_STATIC_CXXFLAGS) -Wno-unused-parameter \
		-c "$(MANAGED_STATIC_CLANG_BUILD)/source/llvm/lib/Support/ManagedStatic.cpp" -o "$@"

$(MANAGED_STATIC_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(MANAGED_STATIC_DIR)/managed-static.mk
	mkdir -p "$(@D)"
	$(MANAGED_STATIC_CC) $(MANAGED_STATIC_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(MANAGED_STATIC_BUILD)/probe.wasm: $(MANAGED_STATIC_BUILD)/probe.o $(MANAGED_STATIC_BUILD)/managed-static.o $(MANAGED_STATIC_BUILD)/crt.o $(MANAGED_STATIC_CLANG_BUILD)/cxx/lib/libc++.a $(MANAGED_STATIC_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(MANAGED_STATIC_DIR)/managed-static.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(MANAGED_STATIC_BUILD)/probe.o" "$(MANAGED_STATIC_BUILD)/managed-static.o" \
		"$(MANAGED_STATIC_BUILD)/crt.o" "$(MANAGED_STATIC_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MANAGED_STATIC_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-managed-static: install-libc aux-test-runner managed-static-fixture
	bash "$(MANAGED_STATIC_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(MANAGED_STATIC_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(MANAGED_STATIC_BUILD)/probe.d $(MANAGED_STATIC_BUILD)/managed-static.d

managed-static-native-source: | managed-static-build-inputs
	mkdir -p "$(MANAGED_STATIC_BUILD)/native"
	$(MANAGED_STATIC_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
		-fno-exceptions -fno-rtti -isystem "$(MANAGED_STATIC_CLANG_BUILD)/native/include" \
		-isystem "$(MANAGED_STATIC_CLANG_BUILD)/source/llvm/include" \
		-I "$(MANAGED_STATIC_CLANG_BUILD)/source/llvm/lib/Support" \
		-c "$(MANAGED_STATIC_DIR)/native.cpp" -o "$(MANAGED_STATIC_BUILD)/native/ManagedStatic.o"
	$(MANAGED_STATIC_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
		-DWASTE_TEST_THREADED_BRANCH -fno-exceptions -fno-rtti \
		-isystem "$(MANAGED_STATIC_CLANG_BUILD)/native/include" \
		-isystem "$(MANAGED_STATIC_CLANG_BUILD)/source/llvm/include" \
		-I "$(MANAGED_STATIC_CLANG_BUILD)/source/llvm/lib/Support" \
		-c "$(MANAGED_STATIC_DIR)/native.cpp" -o "$(MANAGED_STATIC_BUILD)/native/ManagedStatic-threaded.o"
