CC ?= cc
BISON ?= bison
FLEX ?= flex
WASM_LD ?= $(shell command -v wasm-ld 2>/dev/null || printf '%s' ../../build/toolchain/usr/bin/wasm-ld)
WASM_LD_LIB ?= $(if $(filter ../../build/toolchain/%,$(WASM_LD)),../../build/toolchain/usr/lib,)
CPPFLAGS ?=
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -Wpedantic -Werror
FREESTANDING_FLAGS := -Ifreestanding -fno-builtin -fno-stack-protector
BUILD_DIR ?= ../../build/c-engine

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
WAST_SOURCES := main_wast.c waste_exec.c wast_encode.c wast_runner.c wast_stream.c wast_simd.c
WAST_HEADERS := wast_types.h wast_encode.h waste_exec.h wast_runner.h wast_stream.h wast_simd.h
WAST_GENERATED_CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wno-unused-but-set-variable
WAST_LEX_CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare

# WAST browser Wasm target
WAST_WASM_TARGET := $(WAST_BUILD_DIR)/waste-wast.wasm
I32_SMOKE_WASM := $(WAST_BUILD_DIR)/i32-smoke.wasm
IMPORT_SMOKE_WASM := $(WAST_BUILD_DIR)/import-smoke.wasm
EXTERN_PROVIDER_WASM := $(WAST_BUILD_DIR)/extern-provider.wasm
EXTERN_CONSUMER_WASM := $(WAST_BUILD_DIR)/extern-consumer.wasm
I32_SMOKE_TARGET := $(WAST_BUILD_DIR)/i32-smoke-sanitize

.PHONY: clean wast-native wast-browser wast-mmap-test i32-smoke

# ---- WAST native runner ----

wast-native: $(WAST_TARGET)

# Fast native parser regression harness.  Inputs are memory-mapped before
# being handed to the same parser entry point used by the runner.
WAST_MMAP_TEST := $(WAST_BUILD_DIR)/wast-mmap-test

wast-mmap-test: $(WAST_MMAP_TEST)

$(WAST_MMAP_TEST): wast_mmap_test.c wast_runner.c wast_encode.c waste_exec.c wast_stream.c wast_simd.c \
		freestanding_lib.c freestanding_native.c \
		$(WAST_HEADERS) $(WAST_TAB_NATIVE_O) $(WAST_LEX_NATIVE_O)
	mkdir -p $(WAST_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(FREESTANDING_FLAGS) -I. -I$(WAST_GEN_DIR) \
		wast_mmap_test.c wast_runner.c wast_encode.c waste_exec.c wast_stream.c wast_simd.c \
		freestanding_lib.c freestanding_native.c \
		$(WAST_TAB_NATIVE_O) $(WAST_LEX_NATIVE_O) -nostdlib -static -o $@

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
		$(FREESTANDING_FLAGS) -I. -I$(WAST_GEN_DIR) \
		-c $(WAST_TAB_C) -o $@

# Compile flex-generated scanner (with relaxed warnings for generated code)
$(WAST_LEX_NATIVE_O): $(WAST_LEX_C) $(WAST_TAB_H) $(WAST_HEADERS)
	$(CC) $(CPPFLAGS) $(WAST_LEX_CFLAGS) \
		$(FREESTANDING_FLAGS) -I. -I$(WAST_GEN_DIR) \
		-c $(WAST_LEX_C) -o $@

# Compile the WAST runner
$(WAST_TARGET): $(WAST_SOURCES) freestanding_lib.c freestanding_native.c $(WAST_HEADERS) $(WAST_TAB_NATIVE_O) $(WAST_LEX_NATIVE_O)
	mkdir -p $(WAST_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		$(FREESTANDING_FLAGS) -I. -I$(WAST_GEN_DIR) \
		$(WAST_SOURCES) \
		freestanding_lib.c freestanding_native.c \
		$(WAST_TAB_NATIVE_O) \
		$(WAST_LEX_NATIVE_O) \
		-nostdlib -static \
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

$(I32_SMOKE_TARGET): ../../tests/c-engine-i32-smoke.c waste_exec.c wast_simd.c waste_exec.h wast_simd.h wast_types.h
	mkdir -p $(WAST_BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I. \
		../../tests/c-engine-i32-smoke.c waste_exec.c wast_simd.c -lm -o $@

WASM_CC_FLAGS := --target=wasm32 -O2 -std=c11 -Wall -Wextra \
	-fno-builtin -DYYDEBUG=0 -nostdlib \
	-I. -Ifreestanding

$(WAST_WASM_TARGET): browser_wast.c freestanding_lib.c waste_exec.c wast_runner.c wast_encode.c wast_stream.c wast_simd.c \
		$(WAST_TAB_C) $(WAST_LEX_C) $(WAST_HEADERS) waste_exec.h
	mkdir -p $(WAST_BUILD_DIR)
	clang $(WASM_CC_FLAGS) \
		-c freestanding_lib.c -o $(WAST_BUILD_DIR)/freestanding_lib.o
	clang $(WASM_CC_FLAGS) \
		-c browser_wast.c -o $(WAST_BUILD_DIR)/browser_wast.o
	clang $(WASM_CC_FLAGS) \
		-c waste_exec.c -o $(WAST_BUILD_DIR)/waste_exec.o
	clang $(WASM_CC_FLAGS) \
		-c wast_runner.c -o $(WAST_BUILD_DIR)/wast_runner.o
	clang $(WASM_CC_FLAGS) \
		-c wast_encode.c -o $(WAST_BUILD_DIR)/wast_encode.o
	clang $(WASM_CC_FLAGS) \
		-c wast_stream.c -o $(WAST_BUILD_DIR)/wast_stream.o
	clang $(WASM_CC_FLAGS) \
		-c wast_simd.c -o $(WAST_BUILD_DIR)/wast_simd.o
	clang $(WASM_CC_FLAGS) -I$(WAST_GEN_DIR) \
		-Wno-unused-but-set-variable \
		-c $(WAST_TAB_C) -o $(WAST_TAB_WASM_O)
	clang $(WASM_CC_FLAGS) -I$(WAST_GEN_DIR) \
		-Wno-unused-function -Wno-unused-parameter -Wno-sign-compare \
		-c $(WAST_LEX_C) -o $(WAST_LEX_WASM_O)
	env LD_LIBRARY_PATH=$(WASM_LD_LIB) $(WASM_LD) --no-entry --export-memory \
		--initial-memory=33554432 --max-memory=268435456 -z stack-size=8388608 \
		$(WAST_BUILD_DIR)/freestanding_lib.o \
		$(WAST_BUILD_DIR)/browser_wast.o $(WAST_BUILD_DIR)/waste_exec.o \
		$(WAST_BUILD_DIR)/wast_runner.o \
		$(WAST_BUILD_DIR)/wast_encode.o $(WAST_BUILD_DIR)/wast_stream.o \
		$(WAST_BUILD_DIR)/wast_simd.o \
		$(WAST_TAB_WASM_O) $(WAST_LEX_WASM_O) \
		-o $(WAST_WASM_TARGET)

clean:
	rm -f $(WAST_TARGET) $(WAST_WASM_TARGET) \
		$(WAST_BUILD_DIR)/browser_wast.o $(WAST_BUILD_DIR)/waste_exec.o
	rm -rf $(WAST_GEN_DIR)
