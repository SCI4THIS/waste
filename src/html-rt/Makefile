WASM_LD ?= $(shell command -v wasm-ld 2>/dev/null || printf '%s' ../../build/engine/toolchain/usr/bin/wasm-ld)
WASM_LD_LIB ?= $(if $(filter ../../build/engine/toolchain/%,$(WASM_LD)),../../build/engine/toolchain/usr/lib,)
PYTHON ?= python3

ENGINE_DIR := ../engine
REPO_ROOT := ../..
BUILD_DIR ?= ../../build/html-rt
ENGINE_BUILD_DIR ?= ../../build/engine
WAST_GEN_DIR := $(ENGINE_BUILD_DIR)/gen

WASM_CC_FLAGS := --target=wasm32 -O2 -std=c11 -Wall -Wextra \
	-fno-builtin -DYYDEBUG=0 -nostdlib \
	-I$(ENGINE_DIR) -I$(ENGINE_DIR)/lib/include

# Engine core sources (platform-independent)
ENGINE_SOURCES := $(ENGINE_DIR)/waste_exec.c $(ENGINE_DIR)/wast_simd.c \
	$(ENGINE_DIR)/wast_encode.c $(ENGINE_DIR)/wast_stream.c \
	$(ENGINE_DIR)/wast_linker.c $(ENGINE_DIR)/wast_runner.c

ENGINE_HEADERS := $(ENGINE_DIR)/wast_types.h $(ENGINE_DIR)/wast_linker.h \
	$(ENGINE_DIR)/wast_encode.h $(ENGINE_DIR)/waste_exec.h \
	$(ENGINE_DIR)/wast_runner.h $(ENGINE_DIR)/wast_stream.h \
	$(ENGINE_DIR)/wast_simd.h

# Freestanding library: shared (portable) sources from engine
LIB_SHARED_SRC := $(ENGINE_DIR)/lib/string.c $(ENGINE_DIR)/lib/stdio_1.c \
	$(ENGINE_DIR)/lib/stdlib_1.c $(ENGINE_DIR)/lib/math.c $(ENGINE_DIR)/lib/errno.c

# Freestanding library: Wasm platform backend (local to html-rt)
LIB_WASM_SRC := lib/stdlib_2.c lib/stdio_2.c lib/unistd_2.c

# Object files
ENGINE_OBJS := $(BUILD_DIR)/wast_linker.o $(BUILD_DIR)/waste_exec.o \
	$(BUILD_DIR)/wast_runner.o $(BUILD_DIR)/wast_encode.o \
	$(BUILD_DIR)/wast_stream.o $(BUILD_DIR)/wast_simd.o

RT_OBJS := $(BUILD_DIR)/browser_api.o $(BUILD_DIR)/posix_stubs.o

WASM_LIB_SHARED_O := $(patsubst $(ENGINE_DIR)/lib/%.c,$(BUILD_DIR)/lib_%.o,$(LIB_SHARED_SRC))
WASM_LIB_WASM_O := $(patsubst lib/%.c,$(BUILD_DIR)/lib_%.o,$(LIB_WASM_SRC))

WAST_TAB_C := $(WAST_GEN_DIR)/wast.tab.c
WAST_TAB_H := $(WAST_GEN_DIR)/wast.tab.h
WAST_LEX_C := $(WAST_GEN_DIR)/wast.lex.c
WAST_TAB_WASM_O := $(BUILD_DIR)/wast.tab.wasm.o
WAST_LEX_WASM_O := $(BUILD_DIR)/wast.lex.wasm.o

WAST_WASM_TARGET := $(BUILD_DIR)/waste-wast.wasm

LIBC_BUILDER := tools/build-waste-libc.py
LIBC_CLIENTS := $(wildcard $(REPO_ROOT)/tests/libc-test/*-client.wast.inc)
LIBC_SOURCES := lib-impl/waste-libc.wat lib-impl/waste-libc-helpers.c \
	lib-impl/waste-libc-extra.c $(LIBC_CLIENTS) $(LIBC_BUILDER)
LIBC_OUTPUT := $(BUILD_DIR)/waste-libc/waste-libc.wasm
LIBC_FIXTURES := $(patsubst $(REPO_ROOT)/tests/libc-test/%-client.wast.inc,\
	$(BUILD_DIR)/waste-libc/tests/%.wast,$(LIBC_CLIENTS))
LIBC_OUTPUTS := $(LIBC_OUTPUT) $(LIBC_FIXTURES)

.PHONY: wast-browser waste-libc gen clean

# ---- Parser generation (delegate to engine) ----

gen:
	$(MAKE) -C $(ENGINE_DIR) BUILD_DIR=$(abspath $(ENGINE_BUILD_DIR)) gen

# ---- Compile engine sources to Wasm objects ----

$(BUILD_DIR)/wast_linker.o: $(ENGINE_DIR)/wast_linker.c $(ENGINE_HEADERS)
	mkdir -p $(BUILD_DIR)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/waste_exec.o: $(ENGINE_DIR)/waste_exec.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wast_runner.o: $(ENGINE_DIR)/wast_runner.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wast_encode.o: $(ENGINE_DIR)/wast_encode.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wast_stream.o: $(ENGINE_DIR)/wast_stream.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wast_simd.o: $(ENGINE_DIR)/wast_simd.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

# ---- Compile html-rt sources to Wasm objects ----

$(BUILD_DIR)/browser_api.o: browser_api.c $(ENGINE_HEADERS) posix_stubs.h
	clang $(WASM_CC_FLAGS) -I. -c $< -o $@

$(BUILD_DIR)/posix_stubs.o: posix_stubs.c posix_stubs.h $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -I. -c $< -o $@

# ---- Compile freestanding library objects ----

$(BUILD_DIR)/lib_%.o: $(ENGINE_DIR)/lib/%.c
	mkdir -p $(BUILD_DIR)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/lib_%.o: lib/%.c
	mkdir -p $(BUILD_DIR)
	clang $(WASM_CC_FLAGS) -c $< -o $@

# ---- Compile generated parser to Wasm objects ----

$(WAST_TAB_WASM_O): $(WAST_TAB_C) $(WAST_TAB_H) $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -I$(WAST_GEN_DIR) \
		-Wno-unused-but-set-variable \
		-c $(WAST_TAB_C) -o $@

$(WAST_LEX_WASM_O): $(WAST_LEX_C) $(WAST_TAB_H) $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -I$(WAST_GEN_DIR) \
		-Wno-unused-function -Wno-unused-parameter -Wno-sign-compare \
		-c $(WAST_LEX_C) -o $@

# ---- Link Wasm binary ----

wast-browser: gen $(WAST_WASM_TARGET)

$(WAST_WASM_TARGET): $(ENGINE_OBJS) $(RT_OBJS) $(WASM_LIB_SHARED_O) $(WASM_LIB_WASM_O) \
		$(WAST_TAB_WASM_O) $(WAST_LEX_WASM_O)
	env LD_LIBRARY_PATH=$(WASM_LD_LIB) $(WASM_LD) --no-entry --export-memory \
		--initial-memory=33554432 --max-memory=268435456 -z stack-size=8388608 \
		$(WASM_LIB_SHARED_O) $(WASM_LIB_WASM_O) \
		$(ENGINE_OBJS) $(RT_OBJS) \
		$(WAST_TAB_WASM_O) $(WAST_LEX_WASM_O) \
		-o $(WAST_WASM_TARGET)

# The builder emits the standalone module and every generated test fixture in
# one transaction. Grouped targets ensure it runs only once when any output is
# missing or older than its WAT, C helper, client, or builder source.
waste-libc: $(LIBC_OUTPUTS)

$(LIBC_OUTPUTS) &: $(LIBC_SOURCES)
	$(PYTHON) $(LIBC_BUILDER) --repo-root $(abspath $(REPO_ROOT))

clean:
	rm -f $(WAST_WASM_TARGET) $(ENGINE_OBJS) $(RT_OBJS) \
		$(WASM_LIB_SHARED_O) $(WASM_LIB_WASM_O) \
		$(WAST_TAB_WASM_O) $(WAST_LEX_WASM_O)
