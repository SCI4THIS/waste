# Compiled C/C++ checkpoint ABI and real guest stack-pointer observations.
CHECKPOINT_DIR := $(REPO_ROOT)/src/system-tests/clang/checkpoint
CHECKPOINT_BUILD := $(abspath $(BUILD_DIR))/clang/checkpoint
CHECKPOINT_CXX_BUILD := $(REPO_ROOT)/build/aux/clang/cxx
CHECKPOINT_CXX_HEADERS := $(CHECKPOINT_CXX_BUILD)/include/c++/v1
CHECKPOINT_CC ?= clang
CHECKPOINT_CXX ?= clang++
CHECKPOINT_FLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"

.PHONY: checkpoint-fixture test-checkpoint
checkpoint-fixture: $(CHECKPOINT_BUILD)/probe-c.wasm $(CHECKPOINT_BUILD)/probe-cxx.wasm

$(CHECKPOINT_BUILD)/probe-c.o: $(CHECKPOINT_DIR)/probe.c $(CHECKPOINT_DIR)/probe.h $(SDK_HEADERS) $(CHECKPOINT_DIR)/checkpoint.mk
	mkdir -p "$(@D)"
	$(CHECKPOINT_CC) $(CHECKPOINT_FLAGS) -std=c11 -Wall -Wextra -Werror -c "$<" -o "$@"

$(CHECKPOINT_BUILD)/probe-cxx.o: $(CHECKPOINT_DIR)/probe.cpp $(CHECKPOINT_DIR)/probe.h $(SDK_HEADERS) $(CHECKPOINT_CXX_BUILD)/.headers $(CHECKPOINT_DIR)/checkpoint.mk
	mkdir -p "$(@D)"
	$(CHECKPOINT_CXX) -isystem "$(CHECKPOINT_CXX_HEADERS)" $(CHECKPOINT_FLAGS) \
		-std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -c "$<" -o "$@"

$(CHECKPOINT_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(CHECKPOINT_DIR)/checkpoint.mk
	mkdir -p "$(@D)"
	$(CHECKPOINT_CC) $(CHECKPOINT_FLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(CHECKPOINT_BUILD)/stack.o: $(CHECKPOINT_DIR)/stack.s $(CHECKPOINT_DIR)/checkpoint.mk
	mkdir -p "$(@D)"
	$(CHECKPOINT_CC) --target=wasm32-unknown-unknown -c "$<" -o "$@"

$(CHECKPOINT_BUILD)/probe-%.wasm: $(CHECKPOINT_BUILD)/probe-%.o $(CHECKPOINT_BUILD)/crt.o $(CHECKPOINT_BUILD)/stack.o $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(CHECKPOINT_DIR)/checkpoint.mk
	$(WASM_LD) --no-entry --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --export=__stack_pointer --global-base=327680 --table-base=1024 \
		"$(CHECKPOINT_BUILD)/probe-$*.o" "$(CHECKPOINT_BUILD)/crt.o" "$(CHECKPOINT_BUILD)/stack.o" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-checkpoint: install-libc aux-test-runner checkpoint-fixture
	bash "$(CHECKPOINT_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(CHECKPOINT_BUILD)" "$(abspath $(WASM_RUNNER))"
