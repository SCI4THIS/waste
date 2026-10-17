# Compiled guest C++ account provider calls; a nonterminal sink captures observations.
ACCOUNTS_DIR := $(REPO_ROOT)/src/system-tests/clang/accounts
ACCOUNTS_BUILD := $(abspath $(BUILD_DIR))/clang/accounts
ACCOUNTS_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
ACCOUNTS_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
ACCOUNTS_CC ?= clang
ACCOUNTS_CXX ?= clang++
ACCOUNTS_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
ACCOUNTS_CXXFLAGS := -isystem "$(ACCOUNTS_CLANG_BUILD)/cxx/include/c++/v1" \
	-isystem "$(ACCOUNTS_CLANG_DIR)/include" $(ACCOUNTS_CFLAGS) \
	-isystem "$(ACCOUNTS_CLANG_BUILD)/guest/include" \
	-isystem "$(ACCOUNTS_CLANG_BUILD)/source/llvm/include" \
	-isystem "$(ACCOUNTS_CLANG_BUILD)/source/clang/include" \
	-isystem "$(ACCOUNTS_CLANG_BUILD)/guest/tools/clang/include" \
	-I "$(ACCOUNTS_CLANG_BUILD)/source/llvm/lib/Support" \
	-std=c++17 -DNDEBUG -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -Wno-unused-parameter -MD -MP

.PHONY: accounts-fixture accounts-build-inputs test-accounts
accounts-fixture: $(ACCOUNTS_BUILD)/probe.wasm
accounts-build-inputs:
	$(MAKE) -C ../aux "$(ACCOUNTS_CLANG_BUILD)/guest/.configured"

$(ACCOUNTS_BUILD)/probe.o: $(ACCOUNTS_DIR)/probe.cpp $(ACCOUNTS_DIR)/accounts.mk $(SDK_HEADERS) $(ACCOUNTS_CLANG_DIR)/clang-waste.patch $(ACCOUNTS_CLANG_DIR)/include/limits.h $(REPO_ROOT)/src/config.h | accounts-build-inputs
	mkdir -p "$(@D)"
	$(ACCOUNTS_CXX) $(ACCOUNTS_CXXFLAGS) -c "$<" -o "$@"

$(ACCOUNTS_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(ACCOUNTS_DIR)/accounts.mk
	mkdir -p "$(@D)"
	$(ACCOUNTS_CC) $(ACCOUNTS_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(ACCOUNTS_BUILD)/probe.wasm: $(ACCOUNTS_BUILD)/probe.o $(ACCOUNTS_BUILD)/crt.o $(ACCOUNTS_CLANG_BUILD)/cxx/lib/libc++.a $(ACCOUNTS_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(ACCOUNTS_DIR)/accounts.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(ACCOUNTS_BUILD)/probe.o" \
		"$(ACCOUNTS_BUILD)/crt.o" "$(ACCOUNTS_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(ACCOUNTS_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-accounts: install-libc aux-test-runner accounts-fixture
	bash "$(ACCOUNTS_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(ACCOUNTS_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(ACCOUNTS_BUILD)/probe.d
