# Actual LLVM raw-stream core; a nonterminal sink captures observations.
RAW_STREAM_DIR := $(REPO_ROOT)/src/system-tests/clang/raw-stream
RAW_STREAM_BUILD := $(abspath $(BUILD_DIR))/clang/raw-stream
RAW_STREAM_CLANG_DIR := $(REPO_ROOT)/src/aux/clang
RAW_STREAM_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
RAW_STREAM_CC ?= clang
RAW_STREAM_CXX ?= clang++
RAW_STREAM_CFLAGS := --target=wasm32-unknown-unknown -O2 -ffreestanding -fno-builtin \
	-fPIC -fno-stack-protector -nostdinc -nostdlib \
	-isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include"
RAW_STREAM_CXXFLAGS := -isystem "$(RAW_STREAM_CLANG_BUILD)/cxx/include/c++/v1" \
	$(RAW_STREAM_CFLAGS) -isystem "$(RAW_STREAM_CLANG_DIR)/include" \
	-isystem "$(RAW_STREAM_CLANG_BUILD)/guest/include" \
	-isystem "$(RAW_STREAM_CLANG_BUILD)/source/llvm/include" \
	-isystem "$(RAW_STREAM_CLANG_BUILD)/source/clang/include" \
	-isystem "$(RAW_STREAM_CLANG_BUILD)/guest/tools/clang/include" \
	-I "$(RAW_STREAM_CLANG_BUILD)/source/llvm/lib/Support" \
	-std=c++17 -DNDEBUG -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror -Wno-unused-parameter -MD -MP

RAW_STREAM_SOURCES := raw_ostream NativeFormatting
RAW_STREAM_OBJECTS := $(addprefix $(RAW_STREAM_BUILD)/,$(addsuffix .o,$(RAW_STREAM_SOURCES)))
RAW_STREAM_OBJECTS += $(RAW_STREAM_BUILD)/colors.o

.PHONY: raw-stream-fixture raw-stream-build-inputs test-raw-stream
raw-stream-fixture: $(RAW_STREAM_BUILD)/probe.wasm
raw-stream-build-inputs:
	$(MAKE) -C ../aux "$(RAW_STREAM_CLANG_BUILD)/guest/.configured"

$(RAW_STREAM_BUILD)/probe.o: $(RAW_STREAM_DIR)/probe.cpp $(RAW_STREAM_DIR)/raw-stream.mk $(SDK_HEADERS) $(RAW_STREAM_CLANG_DIR)/clang-waste.patch | raw-stream-build-inputs
	mkdir -p "$(@D)"
	$(RAW_STREAM_CXX) $(RAW_STREAM_CXXFLAGS) -c "$<" -o "$@"

$(addprefix $(RAW_STREAM_BUILD)/,$(addsuffix .o,$(RAW_STREAM_SOURCES))): $(RAW_STREAM_BUILD)/%.o: $(RAW_STREAM_CLANG_BUILD)/source/llvm/lib/Support/%.cpp $(RAW_STREAM_DIR)/raw-stream.mk $(SDK_HEADERS) | raw-stream-build-inputs
	mkdir -p "$(@D)"
	$(RAW_STREAM_CXX) $(RAW_STREAM_CXXFLAGS) -c "$<" -o "$@"

$(RAW_STREAM_BUILD)/colors.cpp: $(RAW_STREAM_DIR)/prepare-colors.py $(RAW_STREAM_CLANG_BUILD)/source/llvm/lib/Support/Process.cpp $(RAW_STREAM_CLANG_BUILD)/source/llvm/lib/Support/Unix/Process.inc | raw-stream-build-inputs
	$(PYTHON) "$<" "$(RAW_STREAM_CLANG_BUILD)/source/llvm/lib/Support" "$@"

$(RAW_STREAM_BUILD)/colors.o: $(RAW_STREAM_BUILD)/colors.cpp $(RAW_STREAM_DIR)/raw-stream.mk $(SDK_HEADERS) | raw-stream-build-inputs
	$(RAW_STREAM_CXX) $(RAW_STREAM_CXXFLAGS) -c "$<" -o "$@"

$(RAW_STREAM_BUILD)/crt.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(SDK_HEADERS) $(RAW_STREAM_DIR)/raw-stream.mk
	mkdir -p "$(@D)"
	$(RAW_STREAM_CC) $(RAW_STREAM_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -c "$<" -o "$@"

$(RAW_STREAM_BUILD)/probe.wasm: $(RAW_STREAM_BUILD)/probe.o $(RAW_STREAM_OBJECTS) $(RAW_STREAM_BUILD)/crt.o $(RAW_STREAM_CLANG_BUILD)/cxx/lib/libc++.a $(RAW_STREAM_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(RAW_STREAM_DIR)/raw-stream.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(RAW_STREAM_BUILD)/probe.o" $(RAW_STREAM_OBJECTS) \
		"$(RAW_STREAM_BUILD)/crt.o" "$(RAW_STREAM_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(RAW_STREAM_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-raw-stream: install-libc aux-test-runner raw-stream-fixture
	bash "$(RAW_STREAM_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(RAW_STREAM_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(RAW_STREAM_BUILD)/probe.d $(RAW_STREAM_OBJECTS:.o=.d)

