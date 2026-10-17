# Observe upstream LLVM's actual bare-Wasm endian header/inline operations.
ENDIAN_DIR := $(REPO_ROOT)/src/system-tests/clang/endian
ENDIAN_BUILD := $(abspath $(BUILD_DIR))/clang/endian
ENDIAN_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
ENDIAN_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
ENDIAN_CC ?= clang
ENDIAN_CXX ?= clang++
ENDIAN_FLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"

.PHONY: endian-fixture endian-build-inputs test-endian
endian-fixture: $(ENDIAN_BUILD)/probe.wasm
endian-build-inputs:
	$(MAKE) -C ../aux "$(ENDIAN_CLANG_BUILD)/guest/.configured"

$(ENDIAN_BUILD)/probe.o: $(ENDIAN_DIR)/probe.cpp $(ENDIAN_DIR)/endian.mk $(SDK_HEADERS) $(ENDIAN_CLANG_DIR)/include/machine/endian.h $(wildcard $(ENDIAN_CLANG_DIR)/*.cmake) $(ENDIAN_CLANG_DIR)/clang.mk | endian-build-inputs
	mkdir -p "$(@D)"
	$(ENDIAN_CXX) -isystem "$(ENDIAN_CLANG_BUILD)/cxx/include/c++/v1" $(ENDIAN_FLAGS) \
		-isystem "$(ENDIAN_CLANG_DIR)/include" \
		-I"$(ENDIAN_CLANG_BUILD)/guest/include" -I"$(ENDIAN_CLANG_BUILD)/source/llvm/include" \
		-std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -MD -MP -c "$<" -o "$@"

$(ENDIAN_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(ENDIAN_DIR)/endian.mk
	mkdir -p "$(@D)"
	$(ENDIAN_CC) $(ENDIAN_FLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(ENDIAN_BUILD)/probe.wasm: $(ENDIAN_BUILD)/probe.o $(ENDIAN_BUILD)/crt.o $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(ENDIAN_DIR)/endian.mk
	$(WASM_LD) --no-entry --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(ENDIAN_BUILD)/probe.o" "$(ENDIAN_BUILD)/crt.o" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-endian: install-libc aux-test-runner endian-fixture
	bash "$(ENDIAN_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(ENDIAN_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(ENDIAN_BUILD)/probe.d
