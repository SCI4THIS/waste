CC ?= cc
BISON ?= bison
FLEX ?= flex
WASM_LD ?= $(shell command -v wasm-ld 2>/dev/null || printf '%s' ../../build/toolchain/usr/bin/wasm-ld)
WASM_LD_LIB ?= $(if $(filter ../../build/toolchain/%,$(WASM_LD)),../../build/toolchain/usr/lib,)
CPPFLAGS ?=
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -Wpedantic -Werror
BUILD_DIR ?= ../../build/c-tail-poc
TARGET := $(BUILD_DIR)/waste-tail-poc
WASM_TARGET := $(BUILD_DIR)/waste-tail-poc.wasm
SOURCES := main.c waste_tail.c
HEADERS := waste_tail.h

# WAST runner targets
WAST_BUILD_DIR ?= $(BUILD_DIR)
WAST_TARGET := $(WAST_BUILD_DIR)/waste-wast
WAST_GEN_DIR := $(WAST_BUILD_DIR)/gen
WAST_TAB_C := $(WAST_GEN_DIR)/wast.tab.c
WAST_TAB_H := $(WAST_GEN_DIR)/wast.tab.h
WAST_LEX_C := $(WAST_GEN_DIR)/wast.lex.c
WAST_TAB_NATIVE_O := $(WAST_BUILD_DIR)/wast.tab.native.o
WAST_LEX_NATIVE_O := $(WAST_BUILD_DIR)/wast.lex.native.o
WAST_TAB_WASM_O := $(WAST_BUILD_DIR)/wast.tab.wasm.o
WAST_LEX_WASM_O := $(WAST_BUILD_DIR)/wast.lex.wasm.o
WAST_SOURCES := main_wast.c waste_exec.c wast_encode.c wast_runner.c wast_stream.c wast_general.c
WAST_HEADERS := wast_types.h wast_encode.h waste_exec.h wast_runner.h wast_stream.h wast_general.h
WAST_GENERATED_CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wno-unused-but-set-variable
WAST_LEX_CFLAGS ?= -O2 -std=c11 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare

# WAST browser Wasm target
WAST_WASM_TARGET := $(WAST_BUILD_DIR)/waste-wast.wasm
I32_SMOKE_WASM := $(WAST_BUILD_DIR)/i32-smoke.wasm
IMPORT_SMOKE_WASM := $(WAST_BUILD_DIR)/import-smoke.wasm
EXTERN_PROVIDER_WASM := $(WAST_BUILD_DIR)/extern-provider.wasm
EXTERN_CONSUMER_WASM := $(WAST_BUILD_DIR)/extern-consumer.wasm
I32_SMOKE_TARGET := $(WAST_BUILD_DIR)/i32-smoke-sanitize

.PHONY: all browser clean sanitize wast-native wast-browser wast-mmap-test i32-smoke

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@

sanitize:
	$(MAKE) CFLAGS="-O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer" \
		TARGET="$(BUILD_DIR)/waste-tail-poc-sanitize"

browser: $(WASM_TARGET)

$(WASM_TARGET): browser.c waste_tail.c $(HEADERS)
	mkdir -p $(BUILD_DIR)
	clang --target=wasm32 -O3 -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fno-builtin -DWASTE_FREESTANDING -nostdlib -c browser.c -o $(BUILD_DIR)/browser.o
	clang --target=wasm32 -O3 -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fno-builtin -DWASTE_FREESTANDING -nostdlib -c waste_tail.c -o $(BUILD_DIR)/waste_tail.o
	env LD_LIBRARY_PATH=$(WASM_LD_LIB) $(WASM_LD) --no-entry --export-memory --initial-memory=262144 \
		--max-memory=67108864 $(BUILD_DIR)/browser.o $(BUILD_DIR)/waste_tail.o -o $@

# ---- WAST native runner ----

wast-native: $(WAST_TARGET)

# Fast native parser regression harness.  Inputs are memory-mapped before
# being handed to the same parser entry point used by the runner.
WAST_MMAP_TEST := $(WAST_BUILD_DIR)/wast-mmap-test

wast-mmap-test: $(WAST_MMAP_TEST)

$(WAST_MMAP_TEST): wast_mmap_test.c wast_runner.c wast_encode.c waste_exec.c wast_stream.c wast_general.c \
		$(WAST_HEADERS) $(WAST_TAB_NATIVE_O) $(WAST_LEX_NATIVE_O)
	mkdir -p $(WAST_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. -I$(WAST_GEN_DIR) \
		wast_mmap_test.c wast_runner.c wast_encode.c waste_exec.c wast_stream.c wast_general.c \
		$(WAST_TAB_NATIVE_O) $(WAST_LEX_NATIVE_O) -lm -o $@

# Generate Bison parser (tab.c and tab.h)
$(WAST_TAB_C) $(WAST_TAB_H): wast.y
	mkdir -p $(WAST_GEN_DIR)
	$(BISON) --defines=$(WAST_TAB_H) -o $(WAST_TAB_C) wast.y

# Generate Flex scanner
$(WAST_LEX_C): wast.l $(WAST_TAB_H)
	mkdir -p $(WAST_GEN_DIR)
	$(FLEX) -o $(WAST_LEX_C) wast.l


# Compile bison-generated parser (with relaxed warnings for generated code)
$(WAST_TAB_NATIVE_O): $(WAST_TAB_C) $(WAST_TAB_H) $(WAST_HEADERS)
	$(CC) $(CPPFLAGS) $(WAST_GENERATED_CFLAGS) \
		-I. -I$(WAST_GEN_DIR) \
		-c $(WAST_TAB_C) -o $@

# Compile flex-generated scanner (with relaxed warnings for generated code)
$(WAST_LEX_NATIVE_O): $(WAST_LEX_C) $(WAST_TAB_H) $(WAST_HEADERS)
	$(CC) $(CPPFLAGS) $(WAST_LEX_CFLAGS) \
		-I. -I$(WAST_GEN_DIR) \
		-c $(WAST_LEX_C) -o $@

# Compile the WAST runner
$(WAST_TARGET): $(WAST_SOURCES) $(WAST_HEADERS) $(WAST_TAB_NATIVE_O) $(WAST_LEX_NATIVE_O)
	mkdir -p $(WAST_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-I. -I$(WAST_GEN_DIR) \
		$(WAST_SOURCES) \
		$(WAST_TAB_NATIVE_O) \
		$(WAST_LEX_NATIVE_O) \
		-lm \
		-o $(WAST_TARGET)

# ---- WAST browser Wasm ----

wast-browser: $(WAST_WASM_TARGET)

i32-smoke: $(I32_SMOKE_TARGET) $(I32_SMOKE_WASM) $(IMPORT_SMOKE_WASM) $(EXTERN_PROVIDER_WASM) $(EXTERN_CONSUMER_WASM)
	ASAN_OPTIONS=detect_leaks=0 $(I32_SMOKE_TARGET) $(I32_SMOKE_WASM) $(IMPORT_SMOKE_WASM) $(EXTERN_PROVIDER_WASM) $(EXTERN_CONSUMER_WASM)

$(I32_SMOKE_WASM): ../../tests/c-engine-i32-smoke.wat
	mkdir -p $(WAST_BUILD_DIR)
	wasm-as --enable-multivalue --enable-reference-types $< -o $@

$(IMPORT_SMOKE_WASM): ../../tests/c-engine-import-smoke.wat
	mkdir -p $(WAST_BUILD_DIR)
	wasm-as $< -o $@

$(EXTERN_PROVIDER_WASM): ../../tests/c-engine-extern-provider.wat
	mkdir -p $(WAST_BUILD_DIR)
	wasm-as --enable-reference-types $< -o $@

$(EXTERN_CONSUMER_WASM): ../../tests/c-engine-extern-consumer.wat
	mkdir -p $(WAST_BUILD_DIR)
	wasm-as --enable-reference-types $< -o $@

$(I32_SMOKE_TARGET): ../../tests/c-engine-i32-smoke.c waste_exec.c waste_exec.h wast_types.h
	mkdir -p $(WAST_BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I. \
		../../tests/c-engine-i32-smoke.c waste_exec.c -lm -o $@

WASM_CC_FLAGS := --target=wasm32 -O2 -std=c11 -Wall -Wextra \
	-fno-builtin -DWASTE_FREESTANDING -DYYDEBUG=0 -nostdlib \
	-I. -Ifreestanding

$(WAST_WASM_TARGET): browser_wast.c waste_exec.c wast_general.c wast_runner.c wast_encode.c wast_stream.c \
		$(WAST_TAB_C) $(WAST_LEX_C) $(WAST_HEADERS) waste_exec.h
	mkdir -p $(WAST_BUILD_DIR)
	clang $(WASM_CC_FLAGS) \
		-c browser_wast.c -o $(WAST_BUILD_DIR)/browser_wast.o
	clang $(WASM_CC_FLAGS) \
		-c waste_exec.c -o $(WAST_BUILD_DIR)/waste_exec.o
	clang $(WASM_CC_FLAGS) \
		-c wast_general.c -o $(WAST_BUILD_DIR)/wast_general.o
	clang $(WASM_CC_FLAGS) \
		-c wast_runner.c -o $(WAST_BUILD_DIR)/wast_runner.o
	clang $(WASM_CC_FLAGS) \
		-c wast_encode.c -o $(WAST_BUILD_DIR)/wast_encode.o
	clang $(WASM_CC_FLAGS) \
		-c wast_stream.c -o $(WAST_BUILD_DIR)/wast_stream.o
	clang $(WASM_CC_FLAGS) -I$(WAST_GEN_DIR) \
		-Wno-unused-but-set-variable \
		-c $(WAST_TAB_C) -o $(WAST_TAB_WASM_O)
	clang $(WASM_CC_FLAGS) -I$(WAST_GEN_DIR) \
		-Wno-unused-function -Wno-unused-parameter -Wno-sign-compare \
		-c $(WAST_LEX_C) -o $(WAST_LEX_WASM_O)
	env LD_LIBRARY_PATH=$(WASM_LD_LIB) $(WASM_LD) --no-entry --export-memory \
		--initial-memory=33554432 --max-memory=268435456 -z stack-size=8388608 \
		$(WAST_BUILD_DIR)/browser_wast.o $(WAST_BUILD_DIR)/waste_exec.o \
		$(WAST_BUILD_DIR)/wast_general.o $(WAST_BUILD_DIR)/wast_runner.o \
		$(WAST_BUILD_DIR)/wast_encode.o $(WAST_BUILD_DIR)/wast_stream.o \
		$(WAST_TAB_WASM_O) $(WAST_LEX_WASM_O) \
		-o $(WAST_WASM_TARGET)

clean:
	rm -f $(TARGET) $(WASM_TARGET) $(BUILD_DIR)/waste-tail-poc-sanitize \
		$(BUILD_DIR)/browser.o $(BUILD_DIR)/waste_tail.o
	rm -f $(WAST_TARGET) $(WAST_WASM_TARGET) \
		$(WAST_BUILD_DIR)/browser_wast.o $(WAST_BUILD_DIR)/waste_exec.o
	rm -rf $(WAST_GEN_DIR)
