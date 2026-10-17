# Actual LLVM backoff, compatibility jitter and yielding deadline waits.
BACKOFF_DIR := $(REPO_ROOT)/src/system-tests/clang/backoff
BACKOFF_BUILD := $(abspath $(BUILD_DIR))/clang/backoff
BACKOFF_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
BACKOFF_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
BACKOFF_CC ?= clang
BACKOFF_CXX ?= clang++
BACKOFF_SESSION_RUNNER ?= $(REPO_ROOT)/build/cli-rt/private/guest-session
BACKOFF_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
BACKOFF_CXXFLAGS := -isystem "$(BACKOFF_CLANG_BUILD)/cxx/include/c++/v1" \
	$(BACKOFF_CFLAGS) -isystem "$(BACKOFF_CLANG_DIR)/include" \
	-isystem "$(BACKOFF_CLANG_BUILD)/guest/include" \
	-isystem "$(BACKOFF_CLANG_BUILD)/source/llvm/include" \
	-isystem "$(BACKOFF_CLANG_BUILD)/source/clang/include" \
	-isystem "$(BACKOFF_CLANG_BUILD)/guest/tools/clang/include" \
	-std=c++17 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -MD -MP

.PHONY: backoff-fixture backoff-build-inputs backoff-native-headers test-backoff test-backoff-controlled
backoff-fixture: $(BACKOFF_BUILD)/probe.wasm $(BACKOFF_BUILD)/controlled.wasm
backoff-build-inputs:
	$(MAKE) -C ../aux "$(BACKOFF_CLANG_BUILD)/guest/.configured"

backoff-native-headers: | backoff-build-inputs
	$(BACKOFF_CXX) -std=c++17 -Wall -Wextra -Werror -fsyntax-only \
		-isystem "$(BACKOFF_CLANG_BUILD)/native/include" \
		-isystem "$(BACKOFF_CLANG_BUILD)/source/llvm/include" "$(BACKOFF_DIR)/native.cpp"
	$(BACKOFF_CXX) -std=c++17 -Wall -Wextra -Werror -fsyntax-only -DWASTE_TEST_THREADED_BRANCH \
		-isystem "$(BACKOFF_CLANG_BUILD)/native/include" \
		-isystem "$(BACKOFF_CLANG_BUILD)/source/llvm/include" "$(BACKOFF_DIR)/native.cpp"

$(BACKOFF_BUILD)/probe.o: $(BACKOFF_DIR)/probe.cpp $(BACKOFF_DIR)/backoff.mk $(SDK_HEADERS) $(BACKOFF_CLANG_DIR)/clang-waste.patch $(BACKOFF_CLANG_DIR)/include/llvm/Support/WasteBackoff.h | backoff-build-inputs
	mkdir -p "$(@D)"
	$(BACKOFF_CXX) $(BACKOFF_CXXFLAGS) -flto -c "$<" -o "$@"

# Compile the real staged implementation, without a substituted clock or wait.
$(BACKOFF_BUILD)/backoff.o: $(BACKOFF_DIR)/backoff.mk $(SDK_HEADERS) $(BACKOFF_CLANG_DIR)/clang-waste.patch $(BACKOFF_CLANG_DIR)/include/llvm/Support/WasteBackoff.h | backoff-build-inputs
	mkdir -p "$(@D)"
	$(BACKOFF_CXX) $(BACKOFF_CXXFLAGS) -Wno-unused-parameter -flto \
		-c "$(BACKOFF_CLANG_BUILD)/source/llvm/lib/Support/ExponentialBackoff.cpp" -o "$@"

$(BACKOFF_BUILD)/controlled.o: $(BACKOFF_DIR)/probe.cpp $(BACKOFF_DIR)/backoff.mk $(SDK_HEADERS) $(BACKOFF_CLANG_DIR)/clang-waste.patch $(BACKOFF_CLANG_DIR)/include/llvm/Support/WasteBackoff.h | backoff-build-inputs
	mkdir -p "$(@D)"
	$(BACKOFF_CXX) $(BACKOFF_CXXFLAGS) -DBACKOFF_CONTROLLED -flto -c "$<" -o "$@"

$(BACKOFF_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(BACKOFF_DIR)/backoff.mk
	mkdir -p "$(@D)"
	$(BACKOFF_CC) $(BACKOFF_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(BACKOFF_BUILD)/%.wasm: $(BACKOFF_BUILD)/%.o $(BACKOFF_BUILD)/backoff.o $(BACKOFF_BUILD)/crt.o $(BACKOFF_CLANG_BUILD)/cxx/lib/libc++.a $(BACKOFF_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(BACKOFF_DIR)/backoff.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(BACKOFF_BUILD)/$*.o" "$(BACKOFF_BUILD)/backoff.o" \
		"$(BACKOFF_BUILD)/crt.o" "$(BACKOFF_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(BACKOFF_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-backoff: install-libc aux-test-runner backoff-fixture
	bash "$(BACKOFF_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(BACKOFF_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(BACKOFF_BUILD)/probe.d $(BACKOFF_BUILD)/backoff.d $(BACKOFF_BUILD)/controlled.d

test-backoff-controlled: install-libc backoff-fixture
	$(MAKE) -C ../cli-rt guest-session guest-session-sanitize
	$(MAKE) -C ../html-rt wast-browser
	cd "$(REPO_ROOT)" && $(PYTHON) tests/guest-session-check.py --scenario clang-backoff \
		--native "$(abspath $(BACKOFF_SESSION_RUNNER))" --vfs-root "$(VFS_ROOT)" \
		--wasm "$(REPO_ROOT)/build/html-rt/waste-wast.wasm"
