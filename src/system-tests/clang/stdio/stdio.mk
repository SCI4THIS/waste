# Compiled C/C++ SDK/varargs boundary; observations only in compiled inputs.
STDIO_DIR := $(REPO_ROOT)/src/system-tests/clang/stdio
STDIO_BUILD := $(abspath $(BUILD_DIR))/clang/stdio
STDIO_CXX_BUILD := $(REPO_ROOT)/build/aux/clang/cxx
STDIO_CXX_HEADERS := $(STDIO_CXX_BUILD)/include/c++/v1
STDIO_CC ?= clang
STDIO_CXX ?= clang++
STDIO_FLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"

.PHONY: stdio-fixture test-stdio
stdio-fixture: $(STDIO_BUILD)/probe-c.wasm $(STDIO_BUILD)/probe-cxx.wasm

$(STDIO_BUILD)/probe-c.o: $(STDIO_DIR)/probe.c $(STDIO_DIR)/probe.h $(SDK_HEADERS) $(STDIO_DIR)/stdio.mk
	mkdir -p "$(@D)"
	$(STDIO_CC) $(STDIO_FLAGS) -std=c11 -Wall -Wextra -Werror -c "$<" -o "$@"

$(STDIO_BUILD)/probe-cxx.o: $(STDIO_DIR)/probe.cpp $(STDIO_DIR)/probe.h $(SDK_HEADERS) $(STDIO_CXX_BUILD)/.headers $(STDIO_DIR)/stdio.mk
	mkdir -p "$(@D)"
	$(STDIO_CXX) -isystem "$(STDIO_CXX_HEADERS)" $(STDIO_FLAGS) \
		-std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -c "$<" -o "$@"

$(STDIO_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(STDIO_DIR)/stdio.mk
	mkdir -p "$(@D)"
	$(STDIO_CC) $(STDIO_FLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(STDIO_BUILD)/probe-%.wasm: $(STDIO_BUILD)/probe-%.o $(STDIO_BUILD)/crt.o $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(STDIO_DIR)/stdio.mk
	$(WASM_LD) --no-entry --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(STDIO_BUILD)/probe-$*.o" "$(STDIO_BUILD)/crt.o" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-stdio: install-libc aux-test-runner stdio-fixture
	bash "$(STDIO_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(STDIO_BUILD)" "$(abspath $(WASM_RUNNER))"
