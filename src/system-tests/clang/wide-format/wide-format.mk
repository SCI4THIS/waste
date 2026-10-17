# Compiled C/C++ SDK/varargs boundary; observations only in compiled inputs.
WIDE_FORMAT_DIR := $(REPO_ROOT)/src/system-tests/clang/wide-format
WIDE_FORMAT_BUILD := $(abspath $(BUILD_DIR))/clang/wide-format
WIDE_FORMAT_CXX_BUILD := $(REPO_ROOT)/build/aux/clang/cxx
WIDE_FORMAT_CXX_HEADERS := $(WIDE_FORMAT_CXX_BUILD)/include/c++/v1
WIDE_FORMAT_CC ?= clang
WIDE_FORMAT_CXX ?= clang++
WIDE_FORMAT_FLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"

.PHONY: wide-format-fixture wide-format-build-inputs test-wide-format
wide-format-fixture: $(WIDE_FORMAT_BUILD)/probe-c.wasm $(WIDE_FORMAT_BUILD)/probe-cxx.wasm
wide-format-build-inputs:
	$(MAKE) -C ../aux clang-sdk-check

$(WIDE_FORMAT_BUILD)/probe-c.o: $(WIDE_FORMAT_DIR)/probe.c $(WIDE_FORMAT_DIR)/probe.h $(SDK_HEADERS) $(WIDE_FORMAT_DIR)/wide-format.mk
	mkdir -p "$(@D)"
	$(WIDE_FORMAT_CC) $(WIDE_FORMAT_FLAGS) -std=c11 -Wall -Wextra -Werror -MMD -MP -c "$<" -o "$@"

$(WIDE_FORMAT_BUILD)/probe-cxx.o: $(WIDE_FORMAT_DIR)/probe.cpp $(WIDE_FORMAT_DIR)/probe.h $(SDK_HEADERS) $(wildcard $(REPO_ROOT)/src/aux/clang/*.cmake) $(REPO_ROOT)/src/aux/clang/clang.mk $(WIDE_FORMAT_DIR)/wide-format.mk | wide-format-build-inputs
	mkdir -p "$(@D)"
	$(WIDE_FORMAT_CXX) -isystem "$(WIDE_FORMAT_CXX_HEADERS)" $(WIDE_FORMAT_FLAGS) \
		-std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -MMD -MP -c "$<" -o "$@"

$(WIDE_FORMAT_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(WIDE_FORMAT_DIR)/wide-format.mk
	mkdir -p "$(@D)"
	$(WIDE_FORMAT_CC) $(WIDE_FORMAT_FLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(WIDE_FORMAT_BUILD)/probe-%.wasm: $(WIDE_FORMAT_BUILD)/probe-%.o $(WIDE_FORMAT_BUILD)/crt.o $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(WIDE_FORMAT_DIR)/wide-format.mk
	$(WASM_LD) --no-entry --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(WIDE_FORMAT_BUILD)/probe-$*.o" "$(WIDE_FORMAT_BUILD)/crt.o" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-wide-format: install-libc aux-test-runner wide-format-fixture
	bash "$(WIDE_FORMAT_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(WIDE_FORMAT_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(WIDE_FORMAT_BUILD)/probe-c.d $(WIDE_FORMAT_BUILD)/probe-cxx.d
