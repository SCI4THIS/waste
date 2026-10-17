# Guest PID queries used by LLVM's actual LockFileManager adaptation.
LOCK_OWNER_DIR := $(REPO_ROOT)/src/system-tests/clang/lock-owner
LOCK_OWNER_BUILD := $(abspath $(BUILD_DIR))/clang/lock-owner
LOCK_OWNER_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
LOCK_OWNER_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
LOCK_OWNER_CC ?= clang
LOCK_OWNER_CXX ?= clang++
LOCK_OWNER_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
LOCK_OWNER_CXXFLAGS := -isystem "$(LOCK_OWNER_CLANG_BUILD)/cxx/include/c++/v1" \
	$(LOCK_OWNER_CFLAGS) -isystem "$(LOCK_OWNER_CLANG_DIR)/include" \
	-isystem "$(LOCK_OWNER_CLANG_BUILD)/guest/include" \
	-isystem "$(LOCK_OWNER_CLANG_BUILD)/source/llvm/include" \
	-isystem "$(LOCK_OWNER_CLANG_BUILD)/source/clang/include" \
	-isystem "$(LOCK_OWNER_CLANG_BUILD)/guest/tools/clang/include" \
	-std=c++17 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -MD -MP


.PHONY: lock-owner-fixture lock-owner-build-inputs lock-owner-native-source test-lock-owner
lock-owner-fixture: $(LOCK_OWNER_BUILD)/probe.wasm
lock-owner-build-inputs:
	$(MAKE) -C ../aux "$(LOCK_OWNER_CLANG_BUILD)/guest/.configured"

$(LOCK_OWNER_BUILD)/probe.o: $(LOCK_OWNER_DIR)/probe.cpp $(LOCK_OWNER_DIR)/lock-owner.mk $(SDK_HEADERS) $(LOCK_OWNER_CLANG_DIR)/include/llvm/Support/WasteLockFile.h | lock-owner-build-inputs
	mkdir -p "$(@D)"
	$(LOCK_OWNER_CXX) $(LOCK_OWNER_CXXFLAGS) -c "$<" -o "$@"

$(LOCK_OWNER_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(LOCK_OWNER_DIR)/lock-owner.mk
	mkdir -p "$(@D)"
	$(LOCK_OWNER_CC) $(LOCK_OWNER_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(LOCK_OWNER_BUILD)/probe.wasm: $(LOCK_OWNER_BUILD)/probe.o $(LOCK_OWNER_BUILD)/crt.o $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(LOCK_OWNER_DIR)/lock-owner.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(LOCK_OWNER_BUILD)/probe.o" "$(LOCK_OWNER_BUILD)/crt.o" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

lock-owner-native-source: | lock-owner-build-inputs
	mkdir -p "$(LOCK_OWNER_BUILD)/native"
	$(LOCK_OWNER_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
		-fno-exceptions -fno-rtti -isystem "$(LOCK_OWNER_CLANG_BUILD)/native/include" \
		-isystem "$(LOCK_OWNER_CLANG_BUILD)/source/llvm/include" \
		-c "$(LOCK_OWNER_CLANG_BUILD)/source/llvm/lib/Support/LockFileManager.cpp" \
		-o "$(LOCK_OWNER_BUILD)/native/LockFileManager.o"

test-lock-owner: install-libc aux-test-runner lock-owner-fixture
	bash "$(LOCK_OWNER_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(LOCK_OWNER_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(LOCK_OWNER_BUILD)/probe.d
