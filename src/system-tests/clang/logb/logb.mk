# Public math constants and actual LLVM APFloat-profile exponent helpers.
LOGB_DIR := $(REPO_ROOT)/src/system-tests/clang/logb
LOGB_BUILD := $(abspath $(BUILD_DIR))/clang/logb
LOGB_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
LOGB_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
LOGB_CC ?= clang
LOGB_CXX ?= clang++
LOGB_FLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"

.PHONY: logb-fixture logb-build-inputs test-logb
logb-fixture: $(LOGB_BUILD)/probe.wasm
logb-build-inputs:
	$(MAKE) -C ../aux "$(LOGB_CLANG_BUILD)/guest/.configured"

$(LOGB_BUILD)/probe.o: $(LOGB_DIR)/probe.cpp $(LOGB_DIR)/logb.mk $(SDK_HEADERS) $(LOGB_CLANG_DIR)/clang-waste.patch | logb-build-inputs
	mkdir -p "$(@D)"
	$(LOGB_CXX) -isystem "$(LOGB_CLANG_BUILD)/cxx/include/c++/v1" $(LOGB_FLAGS) \
		-isystem "$(LOGB_CLANG_BUILD)/source/libc" \
		-std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -MD -MP -c "$<" -o "$@"

$(LOGB_BUILD)/constants.o: $(LOGB_DIR)/constants.c $(LOGB_DIR)/logb.mk $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(LOGB_CC) $(LOGB_FLAGS) -std=c11 -Wall -Wextra -Werror -MD -MP -c "$<" -o "$@"

$(LOGB_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(LOGB_DIR)/logb.mk
	mkdir -p "$(@D)"
	$(LOGB_CC) $(LOGB_FLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(LOGB_BUILD)/probe.wasm: $(LOGB_BUILD)/probe.o $(LOGB_BUILD)/constants.o $(LOGB_BUILD)/crt.o $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(LOGB_DIR)/logb.mk
	$(WASM_LD) --no-entry --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(LOGB_BUILD)/probe.o" "$(LOGB_BUILD)/constants.o" "$(LOGB_BUILD)/crt.o" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-logb: install-libc aux-test-runner logb-fixture
	bash "$(LOGB_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(LOGB_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(LOGB_BUILD)/probe.d $(LOGB_BUILD)/constants.d
