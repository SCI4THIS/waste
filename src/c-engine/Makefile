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
WAST_SOURCES := main_wast.c waste_exec.c wast_encode.c wast_runner.c wast_general.c
WAST_HEADERS := wast_types.h wast_encode.h waste_exec.h wast_runner.h wast_general.h

# WAST browser Wasm target
WAST_WASM_TARGET := $(WAST_BUILD_DIR)/waste-wast.wasm
I32_SMOKE_WASM := $(WAST_BUILD_DIR)/i32-smoke.wasm
IMPORT_SMOKE_WASM := $(WAST_BUILD_DIR)/import-smoke.wasm
EXTERN_PROVIDER_WASM := $(WAST_BUILD_DIR)/extern-provider.wasm
EXTERN_CONSUMER_WASM := $(WAST_BUILD_DIR)/extern-consumer.wasm
I32_SMOKE_TARGET := $(WAST_BUILD_DIR)/i32-smoke-sanitize

.PHONY: all browser clean sanitize wast-native wast-browser i32-smoke

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

# Generate Bison parser (tab.c and tab.h)
$(WAST_TAB_C) $(WAST_TAB_H): wast.y
	mkdir -p $(WAST_GEN_DIR)
	$(BISON) --defines=$(WAST_TAB_H) -o $(WAST_TAB_C) wast.y

# Generate Flex scanner
$(WAST_LEX_C): wast.l $(WAST_TAB_H)
	mkdir -p $(WAST_GEN_DIR)
	$(FLEX) -o $(WAST_LEX_C) wast.l

# Compile bison-generated parser (with relaxed warnings for generated code)
$(WAST_BUILD_DIR)/wast.tab.o: $(WAST_TAB_C) $(WAST_TAB_H)
	$(CC) $(CPPFLAGS) -O2 -std=c11 -Wall -Wextra \
		-Wno-unused-but-set-variable \
		-I. -I$(WAST_GEN_DIR) \
		-c $(WAST_TAB_C) -o $@

# Compile flex-generated scanner (with relaxed warnings for generated code)
$(WAST_BUILD_DIR)/wast.lex.o: $(WAST_LEX_C) $(WAST_TAB_H)
	$(CC) $(CPPFLAGS) -O2 -std=c11 -Wall -Wextra \
		-D_POSIX_C_SOURCE=200809L \
		-Wno-unused-function -Wno-unused-parameter -Wno-sign-compare \
		-I. -I$(WAST_GEN_DIR) \
		-c $(WAST_LEX_C) -o $@

# Compile the WAST runner
$(WAST_TARGET): $(WAST_SOURCES) $(WAST_HEADERS) $(WAST_BUILD_DIR)/wast.tab.o $(WAST_BUILD_DIR)/wast.lex.o
	mkdir -p $(WAST_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-I. -I$(WAST_GEN_DIR) \
		$(WAST_SOURCES) \
		$(WAST_BUILD_DIR)/wast.tab.o \
		$(WAST_BUILD_DIR)/wast.lex.o \
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

$(WAST_WASM_TARGET): browser_wast.c waste_exec.c wast_general.c $(WAST_HEADERS) waste_exec.h
	mkdir -p $(WAST_BUILD_DIR)
	clang --target=wasm32 -O2 -std=c11 -Wall -Wextra \
		-fno-builtin -DWASTE_FREESTANDING -nostdlib \
		-I. \
		-c browser_wast.c -o $(WAST_BUILD_DIR)/browser_wast.o
	clang --target=wasm32 -O2 -std=c11 -Wall -Wextra \
		-fno-builtin -DWASTE_FREESTANDING -nostdlib \
		-I. \
		-c waste_exec.c -o $(WAST_BUILD_DIR)/waste_exec.o
	clang --target=wasm32 -O2 -std=c11 -Wall -Wextra \
		-fno-builtin -DWASTE_FREESTANDING -nostdlib \
		-I. \
		-c wast_general.c -o $(WAST_BUILD_DIR)/wast_general.o
	env LD_LIBRARY_PATH=$(WASM_LD_LIB) $(WASM_LD) --no-entry --export-memory \
		--initial-memory=2097152 --max-memory=67108864 -z stack-size=1048576 \
		$(WAST_BUILD_DIR)/browser_wast.o $(WAST_BUILD_DIR)/waste_exec.o \
		$(WAST_BUILD_DIR)/wast_general.o \
		-o $(WAST_WASM_TARGET)

clean:
	rm -f $(TARGET) $(WASM_TARGET) $(BUILD_DIR)/waste-tail-poc-sanitize \
		$(BUILD_DIR)/browser.o $(BUILD_DIR)/waste_tail.o
	rm -f $(WAST_TARGET) $(WAST_WASM_TARGET) \
		$(WAST_BUILD_DIR)/browser_wast.o $(WAST_BUILD_DIR)/waste_exec.o
	rm -rf $(WAST_GEN_DIR)
