CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -Wpedantic -Werror

ENGINE_DIR := ../engine
BUILD_DIR ?= ../../build/engine
WAST_GEN_DIR := $(BUILD_DIR)/gen
FREESTANDING_FLAGS := -I$(ENGINE_DIR)/lib/include -fno-builtin -fno-stack-protector

# Engine core sources (platform-independent)
ENGINE_SOURCES := $(ENGINE_DIR)/waste_exec.c $(ENGINE_DIR)/wast_simd.c \
	$(ENGINE_DIR)/wast_encode.c $(ENGINE_DIR)/wast_stream.c \
	$(ENGINE_DIR)/wast_linker.c $(ENGINE_DIR)/wast_runner.c

ENGINE_HEADERS := $(ENGINE_DIR)/wast_types.h $(ENGINE_DIR)/wast_linker.h \
	$(ENGINE_DIR)/wast_encode.h $(ENGINE_DIR)/waste_exec.h \
	$(ENGINE_DIR)/wast_runner.h $(ENGINE_DIR)/wast_stream.h \
	$(ENGINE_DIR)/wast_simd.h

# Freestanding library: shared (portable) sources from engine
LIB_SHARED := $(ENGINE_DIR)/lib/string.c $(ENGINE_DIR)/lib/stdio_1.c \
	$(ENGINE_DIR)/lib/stdlib_1.c $(ENGINE_DIR)/lib/math.c $(ENGINE_DIR)/lib/errno.c

# Freestanding library: native platform backend (local to cli-rt)
LIB_NATIVE := lib/stdlib_2_native.c lib/stdio_2_native.c lib/fcntl_2_native.c \
	lib/mman_2_native.c lib/time_2_native.c lib/unistd_2_native.c

# Generated parser objects
WAST_TAB_C := $(WAST_GEN_DIR)/wast.tab.c
WAST_TAB_H := $(WAST_GEN_DIR)/wast.tab.h
WAST_LEX_C := $(WAST_GEN_DIR)/wast.lex.c
WAST_TAB_O := $(BUILD_DIR)/wast.tab.native.o
WAST_LEX_O := $(BUILD_DIR)/wast.lex.native.o

WAST_GENERATED_CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wno-unused-but-set-variable
WAST_LEX_CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare

# Targets
WAST_TARGET := $(BUILD_DIR)/waste-wast
WAST_MMAP_TEST := $(BUILD_DIR)/wast-mmap-test
I32_SMOKE_TARGET := $(BUILD_DIR)/i32-smoke-sanitize
I32_SMOKE_WASM := $(BUILD_DIR)/i32-smoke.wasm
IMPORT_SMOKE_WASM := $(BUILD_DIR)/import-smoke.wasm
EXTERN_PROVIDER_WASM := $(BUILD_DIR)/extern-provider.wasm
EXTERN_CONSUMER_WASM := $(BUILD_DIR)/extern-consumer.wasm

.PHONY: wast-native wast-mmap-test i32-smoke gen clean

# ---- Parser generation (delegate to engine) ----

gen:
	$(MAKE) -C $(ENGINE_DIR) BUILD_DIR=$(abspath $(BUILD_DIR)) gen

# ---- Compile generated parser objects ----

$(WAST_TAB_O): $(WAST_TAB_C) $(WAST_TAB_H) $(ENGINE_HEADERS)
	$(CC) $(CPPFLAGS) $(WAST_GENERATED_CFLAGS) \
		$(FREESTANDING_FLAGS) -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) \
		-c $(WAST_TAB_C) -o $@

$(WAST_LEX_O): $(WAST_LEX_C) $(WAST_TAB_H) $(ENGINE_HEADERS)
	$(CC) $(CPPFLAGS) $(WAST_LEX_CFLAGS) \
		$(FREESTANDING_FLAGS) -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) \
		-c $(WAST_LEX_C) -o $@

# ---- WAST native runner ----

wast-native: gen $(WAST_TARGET)

$(WAST_TARGET): main_wast.c $(ENGINE_SOURCES) $(LIB_SHARED) $(LIB_NATIVE) \
		$(ENGINE_HEADERS) $(WAST_TAB_O) $(WAST_LEX_O)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		$(FREESTANDING_FLAGS) -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) \
		main_wast.c $(ENGINE_SOURCES) \
		$(LIB_SHARED) $(LIB_NATIVE) \
		$(WAST_TAB_O) $(WAST_LEX_O) \
		-nostdlib -static \
		-o $(WAST_TARGET)

# ---- WAST mmap parse benchmark ----

wast-mmap-test: gen $(WAST_MMAP_TEST)

$(WAST_MMAP_TEST): wast_mmap_test.c $(ENGINE_SOURCES) $(LIB_SHARED) $(LIB_NATIVE) \
		$(ENGINE_HEADERS) $(WAST_TAB_O) $(WAST_LEX_O)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(FREESTANDING_FLAGS) -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) \
		wast_mmap_test.c $(ENGINE_SOURCES) \
		$(LIB_SHARED) $(LIB_NATIVE) \
		$(WAST_TAB_O) $(WAST_LEX_O) -nostdlib -static -o $@

# ---- i32 smoke test (ASan/UBSan, uses system libc) ----

i32-smoke: $(I32_SMOKE_TARGET) $(I32_SMOKE_WASM) $(IMPORT_SMOKE_WASM) $(EXTERN_PROVIDER_WASM) $(EXTERN_CONSUMER_WASM)
	ASAN_OPTIONS=detect_leaks=0 $(I32_SMOKE_TARGET) $(I32_SMOKE_WASM) $(IMPORT_SMOKE_WASM) $(EXTERN_PROVIDER_WASM) $(EXTERN_CONSUMER_WASM)

$(I32_SMOKE_WASM): ../../tests/c-engine-i32-smoke.wat
	mkdir -p $(BUILD_DIR)
	wasm-as --enable-multivalue --enable-reference-types $< -o $@

$(IMPORT_SMOKE_WASM): ../../tests/c-engine-import-smoke.wat
	mkdir -p $(BUILD_DIR)
	wasm-as $< -o $@

$(EXTERN_PROVIDER_WASM): ../../tests/c-engine-extern-provider.wat
	mkdir -p $(BUILD_DIR)
	wasm-as --enable-reference-types $< -o $@

$(EXTERN_CONSUMER_WASM): ../../tests/c-engine-extern-consumer.wat
	mkdir -p $(BUILD_DIR)
	wasm-as --enable-reference-types $< -o $@

$(I32_SMOKE_TARGET): ../../tests/c-engine-i32-smoke.c $(ENGINE_DIR)/waste_exec.c $(ENGINE_DIR)/wast_simd.c \
		$(ENGINE_DIR)/waste_exec.h $(ENGINE_DIR)/wast_simd.h $(ENGINE_DIR)/wast_types.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I$(ENGINE_DIR) \
		../../tests/c-engine-i32-smoke.c $(ENGINE_DIR)/waste_exec.c $(ENGINE_DIR)/wast_simd.c -lm -o $@

clean:
	rm -f $(WAST_TARGET) $(WAST_MMAP_TEST) $(I32_SMOKE_TARGET) \
		$(WAST_TAB_O) $(WAST_LEX_O)
