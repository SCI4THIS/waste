# Compiled C/C++ classification boundary; WAST owns functional assertions.
CLASSIFICATION_DIR := $(REPO_ROOT)/src/aux/clang/test/classification
CLASSIFICATION_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/classification
CLASSIFICATION_CXX_BUILD := $(REPO_ROOT)/build/aux/clang/cxx
CLASSIFICATION_CXX_HEADERS := $(CLASSIFICATION_CXX_BUILD)/include/c++/v1
CLASSIFICATION_CC ?= clang
CLASSIFICATION_CXX ?= clang++
CLASSIFICATION_FLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"

.PHONY: classification-fixture test-classification
classification-fixture: $(CLASSIFICATION_BUILD)/probe-c.wasm $(CLASSIFICATION_BUILD)/probe-cxx.wasm

$(CLASSIFICATION_BUILD)/probe-c.o: $(CLASSIFICATION_DIR)/probe.c $(CLASSIFICATION_DIR)/probe.h $(SDK_HEADERS) $(CLASSIFICATION_DIR)/classification.mk
	mkdir -p "$(@D)"
	$(CLASSIFICATION_CC) $(CLASSIFICATION_FLAGS) -std=c11 -Wall -Wextra -Werror -c "$<" -o "$@"

$(CLASSIFICATION_BUILD)/probe-cxx.o: $(CLASSIFICATION_DIR)/probe.cpp $(CLASSIFICATION_DIR)/probe.h $(SDK_HEADERS) $(CLASSIFICATION_CXX_BUILD)/.headers $(CLASSIFICATION_DIR)/classification.mk
	mkdir -p "$(@D)"
	$(CLASSIFICATION_CXX) -isystem "$(CLASSIFICATION_CXX_HEADERS)" $(CLASSIFICATION_FLAGS) \
		-std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -c "$<" -o "$@"

$(CLASSIFICATION_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(CLASSIFICATION_DIR)/classification.mk
	mkdir -p "$(@D)"
	$(CLASSIFICATION_CC) $(CLASSIFICATION_FLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(CLASSIFICATION_BUILD)/probe-%.wasm: $(CLASSIFICATION_BUILD)/probe-%.o $(CLASSIFICATION_BUILD)/crt.o $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(CLASSIFICATION_DIR)/classification.mk
	$(WASM_LD) --no-entry --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(CLASSIFICATION_BUILD)/probe-$*.o" "$(CLASSIFICATION_BUILD)/crt.o" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-classification: install-libc aux-test-runner classification-fixture
	bash "$(CLASSIFICATION_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(CLASSIFICATION_BUILD)" "$(abspath $(WASM_RUNNER))"
