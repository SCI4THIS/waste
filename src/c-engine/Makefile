CC ?= cc
WASM_LD ?= $(shell command -v wasm-ld 2>/dev/null || printf '%s' ../../build/toolchain/usr/bin/wasm-ld)
WASM_LD_LIB ?= $(if $(filter ../../build/toolchain/%,$(WASM_LD)),../../build/toolchain/usr/lib,)
CPPFLAGS ?=
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -Wpedantic -Werror
BUILD_DIR ?= ../../build/c-tail-poc
TARGET := $(BUILD_DIR)/waste-tail-poc
WASM_TARGET := $(BUILD_DIR)/waste-tail-poc.wasm
SOURCES := main.c waste_tail.c
HEADERS := waste_tail.h

.PHONY: all browser clean sanitize

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

clean:
	rm -f $(TARGET) $(WASM_TARGET) $(BUILD_DIR)/waste-tail-poc-sanitize \
		$(BUILD_DIR)/browser.o $(BUILD_DIR)/waste_tail.o
