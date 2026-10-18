# Generated wide libc++ headers and real shared-libc calls, without stale archives.
WIDE_DIR := $(REPO_ROOT)/src/aux/clang/test/wide
WIDE_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/wide
WIDE_CXX_BUILD := $(REPO_ROOT)/build/aux/clang/cxx
WIDE_ABORT_SOURCE := $(REPO_ROOT)/build/aux/clang/source/libcxx/src/verbose_abort.cpp
WIDE_FLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"

.PHONY: wide-fixture wide-build-inputs test-wide
wide-fixture: $(WIDE_BUILD)/probe.wasm
wide-build-inputs:
	$(MAKE) -C ../aux clang-sdk-check

$(WIDE_BUILD)/probe.o: $(WIDE_DIR)/probe.cpp $(WIDE_DIR)/wide.mk $(SDK_HEADERS) $(wildcard $(REPO_ROOT)/src/aux/clang/*.cmake) $(REPO_ROOT)/src/aux/clang/clang.mk | wide-build-inputs
	mkdir -p "$(@D)"
	clang++ -isystem "$(WIDE_CXX_BUILD)/include/c++/v1" $(WIDE_FLAGS) \
		-std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -MD -MP -c "$<" -o "$@"

$(WIDE_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(WIDE_DIR)/wide.mk
	mkdir -p "$(@D)"
	clang $(WIDE_FLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(WIDE_ABORT_SOURCE): | wide-build-inputs
	test -f "$@"

$(WIDE_BUILD)/verbose-abort.o: $(WIDE_ABORT_SOURCE) $(SDK_HEADERS) $(WIDE_DIR)/wide.mk | wide-build-inputs
	mkdir -p "$(@D)"
	clang++ -isystem "$(WIDE_CXX_BUILD)/include/c++/v1" $(WIDE_FLAGS) \
		-std=c++26 -fno-exceptions -fno-rtti -D_LIBCPP_BUILDING_LIBRARY \
		-D_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER -D_LIBCPP_AVAILABILITY_MINIMUM_HEADER_VERSION=2 \
		-Wall -Wextra -Werror -MD -MP -c "$<" -o "$@"

$(WIDE_BUILD)/probe.wasm: $(WIDE_BUILD)/probe.o $(WIDE_BUILD)/crt.o $(WIDE_BUILD)/verbose-abort.o $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(WIDE_DIR)/wide.mk
	$(WASM_LD) --no-entry --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(WIDE_BUILD)/probe.o" "$(WIDE_BUILD)/crt.o" "$(WIDE_BUILD)/verbose-abort.o" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-wide: install-libc aux-test-runner wide-fixture
	bash "$(WIDE_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(WIDE_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(WIDE_BUILD)/probe.d
-include $(WIDE_BUILD)/verbose-abort.d
