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
WAST_SOURCES := main_wast.c waste_exec.c wast_encode.c wast_runner.c
WAST_HEADERS := wast_types.h wast_encode.h waste_exec.h wast_runner.h

# WAST browser Wasm target
WAST_WASM_TARGET := $(WAST_BUILD_DIR)/waste-wast.wasm

.PHONY: all browser clean sanitize wast-native wast-browser

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

$(WAST_WASM_TARGET): browser_wast.c waste_exec.c $(WAST_HEADERS) waste_exec.h
	mkdir -p $(WAST_BUILD_DIR)
	clang --target=wasm32 -O2 -std=c11 -Wall -Wextra \
		-fno-builtin -DWASTE_FREESTANDING -nostdlib \
		-I. \
		-c browser_wast.c -o $(WAST_BUILD_DIR)/browser_wast.o
	clang --target=wasm32 -O2 -std=c11 -Wall -Wextra \
		-fno-builtin -DWASTE_FREESTANDING -nostdlib \
		-I. \
		-c waste_exec.c -o $(WAST_BUILD_DIR)/waste_exec.o
	env LD_LIBRARY_PATH=$(WASM_LD_LIB) $(WASM_LD) --no-entry --export-memory \
		--initial-memory=262144 --max-memory=67108864 \
		$(WAST_BUILD_DIR)/browser_wast.o $(WAST_BUILD_DIR)/waste_exec.o \
		-o $(WAST_WASM_TARGET)

clean:
	rm -f $(TARGET) $(WASM_TARGET) $(BUILD_DIR)/waste-tail-poc-sanitize \
		$(BUILD_DIR)/browser.o $(BUILD_DIR)/waste_tail.o
	rm -f $(WAST_TARGET) $(WAST_WASM_TARGET) \
		$(WAST_BUILD_DIR)/browser_wast.o $(WAST_BUILD_DIR)/waste_exec.o
	rm -rf $(WAST_GEN_DIR)
