CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -Wpedantic -Werror

ENGINE_DIR := ../engine
BUILD_DIR ?= ../../build/cli-rt
ENGINE_BUILD_DIR ?= ../../build/engine
WAST_GEN_DIR := $(ENGINE_BUILD_DIR)/gen
FREESTANDING_FLAGS := -I$(ENGINE_DIR)/lib/include -fno-builtin -fno-stack-protector

# Engine core sources (platform-independent)
ENGINE_SOURCES := $(ENGINE_DIR)/wasm/wasm_load.c \
	$(ENGINE_DIR)/wasm/wasm_opcode.c $(ENGINE_DIR)/wasm/wasm_encode.c \
	$(ENGINE_DIR)/script/wast_stream.c $(ENGINE_DIR)/script/wast_command.c \
	$(ENGINE_DIR)/runtime/store.c \
	$(ENGINE_DIR)/script/wast_runner.c $(ENGINE_DIR)/script/wast_assert.c \
	$(ENGINE_DIR)/text/wat_builder.c $(ENGINE_DIR)/text/wat_literal.c \
	$(ENGINE_DIR)/wasm/wasm_leb.c $(ENGINE_DIR)/wasm/wasm_reader.c \
	$(ENGINE_DIR)/wasm/wasm_writer.c $(ENGINE_DIR)/wasm/wasm_module.c \
	$(ENGINE_DIR)/wasm/wasm_decode.c $(ENGINE_DIR)/wasm/wasm_validate.c \
	$(ENGINE_DIR)/runtime/instantiate.c $(ENGINE_DIR)/runtime/execute.c \
	$(ENGINE_DIR)/runtime/execute_memory.c \
	$(ENGINE_DIR)/runtime/execute_numeric.c \
	$(ENGINE_DIR)/runtime/execute_simd.c $(ENGINE_DIR)/runtime/execute_gc.c \
	$(ENGINE_DIR)/runtime/execute_exception.c \
	$(ENGINE_DIR)/runtime/execute_table.c $(ENGINE_DIR)/runtime/instance.c \
	$(ENGINE_DIR)/runtime/api.c

ENGINE_HEADERS := $(ENGINE_DIR)/text/wat_types.h $(ENGINE_DIR)/runtime/store.h \
	$(ENGINE_DIR)/wasm/wasm_encode.h $(ENGINE_DIR)/runtime/engine_internal.h \
	$(ENGINE_DIR)/script/wast_runner.h $(ENGINE_DIR)/script/wast_stream.h \
	$(ENGINE_DIR)/script/wast_assert.h $(ENGINE_DIR)/script/wast_command.h \
	$(ENGINE_DIR)/wasm/wasm_opcode.h \
	$(ENGINE_DIR)/text/wat_context.h $(ENGINE_DIR)/text/wat_literal.h \
	$(ENGINE_DIR)/text/wat_builder.h \
	$(ENGINE_DIR)/wasm/wasm_leb.h \
	$(ENGINE_DIR)/wasm/wasm_reader.h $(ENGINE_DIR)/wasm/wasm_writer.h \
	$(ENGINE_DIR)/wasm/wasm_module.h $(ENGINE_DIR)/wasm/wasm_decode.h \
	$(ENGINE_DIR)/wasm/wasm_validate.h \
	$(ENGINE_DIR)/runtime/instantiate.h $(ENGINE_DIR)/runtime/runtime_internal.h

ENGINE_HEADERS += $(ENGINE_DIR)/include/waste_engine.h \
	$(ENGINE_DIR)/include/waste_error.h $(ENGINE_DIR)/include/waste_value.h

# Freestanding library: shared (portable) sources from engine
LIB_SHARED := $(ENGINE_DIR)/lib/string.c $(ENGINE_DIR)/lib/stdio.c \
	$(ENGINE_DIR)/lib/stdlib.c $(ENGINE_DIR)/lib/math.c $(ENGINE_DIR)/lib/errno.c

# Freestanding library: native platform backend (local to cli-rt)
LIB_NATIVE := lib/stdlib.c lib/stdio.c lib/fcntl.c \
	lib/mman.c lib/time.c lib/unistd.c

# Generated parser objects
WAST_TAB_C := $(WAST_GEN_DIR)/wast.tab.c
WAST_TAB_H := $(WAST_GEN_DIR)/wast.tab.h
WAST_LEX_C := $(WAST_GEN_DIR)/wast.lex.c
WAST_TAB_O := $(BUILD_DIR)/wast.tab.native.o
WAST_LEX_O := $(BUILD_DIR)/wast.lex.native.o
WAST_TAB_SAN_O := $(BUILD_DIR)/wast.tab.sanitize.o
WAST_LEX_SAN_O := $(BUILD_DIR)/wast.lex.sanitize.o

WAST_GENERATED_CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wno-unused-but-set-variable
WAST_LEX_CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare

# Targets
WAST_TARGET := $(BUILD_DIR)/waste-cli
I32_SMOKE_TARGET := $(BUILD_DIR)/i32-smoke-sanitize
WASM_BINARY_TEST_TARGET := $(BUILD_DIR)/wasm-binary-primitives-sanitize
WASM_IMPORT_TEST_TARGET := $(BUILD_DIR)/wasm-import-decode-sanitize
WASM_VALIDATION_TEST_TARGET := $(BUILD_DIR)/wasm-validation-sanitize
PARSER_REENTRANT_TEST_TARGET := $(BUILD_DIR)/wast-parser-reentrant-sanitize
I32_SMOKE_WASM := $(BUILD_DIR)/i32-smoke.wasm
IMPORT_SMOKE_WASM := $(BUILD_DIR)/import-smoke.wasm
EXTERN_PROVIDER_WASM := $(BUILD_DIR)/extern-provider.wasm
EXTERN_CONSUMER_WASM := $(BUILD_DIR)/extern-consumer.wasm

.PHONY: wast-native i32-smoke wasm-binary-primitives wasm-import-decode \
	wasm-validation parser-reentrant gen clean

# ---- Parser generation (delegate to engine) ----

gen:
	$(MAKE) -C $(ENGINE_DIR) BUILD_DIR=$(abspath $(ENGINE_BUILD_DIR)) gen

# ---- Compile generated parser objects ----

$(WAST_TAB_O): $(WAST_TAB_C) $(WAST_TAB_H) $(ENGINE_HEADERS)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(WAST_GENERATED_CFLAGS) \
		$(FREESTANDING_FLAGS) -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) \
		-c $(WAST_TAB_C) -o $@

$(WAST_LEX_O): $(WAST_LEX_C) $(WAST_TAB_H) $(ENGINE_HEADERS)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(WAST_LEX_CFLAGS) \
		$(FREESTANDING_FLAGS) -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) \
		-c $(WAST_LEX_C) -o $@

$(WAST_TAB_SAN_O): $(WAST_TAB_C) $(WAST_TAB_H) $(ENGINE_HEADERS)
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wno-unused-but-set-variable \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-I$(ENGINE_DIR) -I$(WAST_GEN_DIR) -c $(WAST_TAB_C) -o $@

$(WAST_LEX_SAN_O): $(WAST_LEX_C) $(WAST_TAB_H) $(ENGINE_HEADERS)
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wno-unused-function \
		-Wno-unused-parameter -Wno-sign-compare \
		-D_POSIX_C_SOURCE=200809L \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-I$(ENGINE_DIR) -I$(WAST_GEN_DIR) -c $(WAST_LEX_C) -o $@

# ---- WAST native runner ----

wast-native: gen $(WAST_TARGET)

$(WAST_TARGET): main.c $(ENGINE_SOURCES) $(LIB_SHARED) $(LIB_NATIVE) \
		$(ENGINE_HEADERS) $(WAST_TAB_O) $(WAST_LEX_O)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		$(FREESTANDING_FLAGS) -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) \
		main.c $(ENGINE_SOURCES) \
		$(LIB_SHARED) $(LIB_NATIVE) \
		$(WAST_TAB_O) $(WAST_LEX_O) \
		-nostdlib -static \
		-o $(WAST_TARGET)

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

$(I32_SMOKE_TARGET): ../../tests/c-engine-i32-smoke.c $(ENGINE_DIR)/wasm/wasm_load.c $(ENGINE_DIR)/wasm/wasm_opcode.c \
		$(ENGINE_DIR)/wasm/wasm_leb.c $(ENGINE_DIR)/wasm/wasm_reader.c \
		$(ENGINE_DIR)/wasm/wasm_module.c $(ENGINE_DIR)/wasm/wasm_decode.c \
		$(ENGINE_DIR)/wasm/wasm_validate.c \
		$(ENGINE_DIR)/runtime/instantiate.c $(ENGINE_DIR)/runtime/execute.c \
		$(ENGINE_DIR)/runtime/execute_memory.c \
		$(ENGINE_DIR)/runtime/execute_numeric.c \
		$(ENGINE_DIR)/runtime/execute_simd.c \
		$(ENGINE_DIR)/runtime/execute_gc.c \
		$(ENGINE_DIR)/runtime/execute_exception.c \
		$(ENGINE_DIR)/runtime/execute_table.c $(ENGINE_DIR)/runtime/instance.c \
		$(ENGINE_DIR)/runtime/api.c \
		$(ENGINE_DIR)/runtime/engine_internal.h $(ENGINE_DIR)/wasm/wasm_opcode.h $(ENGINE_DIR)/text/wat_types.h \
		$(ENGINE_DIR)/wasm/wasm_leb.h $(ENGINE_DIR)/wasm/wasm_reader.h \
		$(ENGINE_DIR)/wasm/wasm_module.h $(ENGINE_DIR)/wasm/wasm_decode.h \
		$(ENGINE_DIR)/wasm/wasm_validate.h \
		$(ENGINE_DIR)/runtime/instantiate.h $(ENGINE_DIR)/runtime/runtime_internal.h \
		$(ENGINE_DIR)/include/waste_engine.h $(ENGINE_DIR)/include/waste_error.h \
		$(ENGINE_DIR)/include/waste_value.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I$(ENGINE_DIR) \
		../../tests/c-engine-i32-smoke.c $(ENGINE_DIR)/wasm/wasm_load.c \
		$(ENGINE_DIR)/wasm/wasm_opcode.c $(ENGINE_DIR)/wasm/wasm_leb.c \
		$(ENGINE_DIR)/wasm/wasm_reader.c $(ENGINE_DIR)/wasm/wasm_module.c \
		$(ENGINE_DIR)/wasm/wasm_decode.c $(ENGINE_DIR)/wasm/wasm_validate.c \
		$(ENGINE_DIR)/runtime/instantiate.c $(ENGINE_DIR)/runtime/execute.c \
		$(ENGINE_DIR)/runtime/execute_memory.c \
		$(ENGINE_DIR)/runtime/execute_numeric.c \
		$(ENGINE_DIR)/runtime/execute_simd.c \
		$(ENGINE_DIR)/runtime/execute_gc.c \
		$(ENGINE_DIR)/runtime/execute_exception.c \
		$(ENGINE_DIR)/runtime/execute_table.c $(ENGINE_DIR)/runtime/instance.c \
		$(ENGINE_DIR)/runtime/api.c \
		-lm -o $@

# ---- Shared binary reader/writer tests (ASan/UBSan, uses system libc) ----

wasm-binary-primitives: $(WASM_BINARY_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0 $(WASM_BINARY_TEST_TARGET)

$(WASM_BINARY_TEST_TARGET): ../../tests/wasm-binary-primitives.c \
		$(ENGINE_DIR)/wasm/wasm_leb.c $(ENGINE_DIR)/wasm/wasm_reader.c \
		$(ENGINE_DIR)/wasm/wasm_writer.c $(ENGINE_DIR)/wasm/wasm_leb.h \
		$(ENGINE_DIR)/wasm/wasm_reader.h $(ENGINE_DIR)/wasm/wasm_writer.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I$(ENGINE_DIR) \
		../../tests/wasm-binary-primitives.c \
		$(ENGINE_DIR)/wasm/wasm_leb.c $(ENGINE_DIR)/wasm/wasm_reader.c \
		$(ENGINE_DIR)/wasm/wasm_writer.c -o $@

# ---- Decoded import declaration tests (ASan/UBSan, uses system libc) ----

wasm-import-decode: $(WASM_IMPORT_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0 $(WASM_IMPORT_TEST_TARGET)

$(WASM_IMPORT_TEST_TARGET): ../../tests/wasm-import-decode.c \
		$(ENGINE_DIR)/wasm/wasm_leb.c $(ENGINE_DIR)/wasm/wasm_reader.c \
		$(ENGINE_DIR)/wasm/wasm_writer.c $(ENGINE_DIR)/wasm/wasm_module.c \
		$(ENGINE_DIR)/wasm/wasm_decode.c $(ENGINE_DIR)/wasm/wasm_leb.h \
		$(ENGINE_DIR)/wasm/wasm_reader.h $(ENGINE_DIR)/wasm/wasm_writer.h \
		$(ENGINE_DIR)/wasm/wasm_module.h $(ENGINE_DIR)/wasm/wasm_decode.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I$(ENGINE_DIR) \
		../../tests/wasm-import-decode.c \
		$(ENGINE_DIR)/wasm/wasm_leb.c $(ENGINE_DIR)/wasm/wasm_reader.c \
		$(ENGINE_DIR)/wasm/wasm_writer.c $(ENGINE_DIR)/wasm/wasm_module.c \
		$(ENGINE_DIR)/wasm/wasm_decode.c -o $@

# ---- Type/control validation tests (ASan/UBSan, uses system libc) ----

wasm-validation: $(WASM_VALIDATION_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0 $(WASM_VALIDATION_TEST_TARGET)

# ---- Concurrent parser isolation test (ASan/UBSan, uses system libc) ----

parser-reentrant: gen $(PARSER_REENTRANT_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0 $(PARSER_REENTRANT_TEST_TARGET)

$(PARSER_REENTRANT_TEST_TARGET): ../../tests/wast-parser-reentrant.c \
		$(ENGINE_SOURCES) $(ENGINE_HEADERS) $(WAST_TAB_SAN_O) $(WAST_LEX_SAN_O)
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-Wno-format-truncation \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-I$(ENGINE_DIR) -I$(WAST_GEN_DIR) \
		../../tests/wast-parser-reentrant.c $(ENGINE_SOURCES) \
		$(WAST_TAB_SAN_O) $(WAST_LEX_SAN_O) -pthread -lm -o $@

$(WASM_VALIDATION_TEST_TARGET): ../../tests/wasm-validation.c \
		$(ENGINE_DIR)/wasm/wasm_validate.c $(ENGINE_DIR)/wasm/wasm_opcode.c \
		$(ENGINE_DIR)/wasm/wasm_validate.h \
		$(ENGINE_DIR)/runtime/runtime_internal.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I$(ENGINE_DIR) \
		../../tests/wasm-validation.c \
		$(ENGINE_DIR)/wasm/wasm_validate.c $(ENGINE_DIR)/wasm/wasm_opcode.c \
		-o $@

clean:
	rm -f $(WAST_TARGET) $(I32_SMOKE_TARGET) $(WASM_BINARY_TEST_TARGET) \
		$(WASM_IMPORT_TEST_TARGET) $(WASM_VALIDATION_TEST_TARGET) \
		$(PARSER_REENTRANT_TEST_TARGET) \
		$(WAST_TAB_O) $(WAST_LEX_O) $(WAST_TAB_SAN_O) $(WAST_LEX_SAN_O)
