WASM_LD ?= $(shell command -v wasm-ld 2>/dev/null || printf '%s' ../../build/engine/toolchain/usr/bin/wasm-ld)
WASM_LD_LIB ?= $(if $(filter ../../build/engine/toolchain/%,$(WASM_LD)),../../build/engine/toolchain/usr/lib,)
PYTHON ?= python3

ENGINE_DIR := ../engine
REPO_ROOT := ../..
BUILD_DIR ?= ../../build/html-rt
ENGINE_BUILD_DIR ?= ../../build/engine
WAST_GEN_DIR := $(ENGINE_BUILD_DIR)/gen

WASM_CC_FLAGS := --target=wasm32 -O2 -std=c11 -Wall -Wextra \
	-fno-builtin -DYYDEBUG=0 -DWASTE_ENGINE -nostdlib \
	-I$(ENGINE_DIR) -I$(ENGINE_DIR)/lib/include -I$(WAST_GEN_DIR)

# Engine core sources (platform-independent)
ENGINE_SOURCES := $(ENGINE_DIR)/wasm/load.c \
	$(ENGINE_DIR)/wasm/opcode.c $(ENGINE_DIR)/wasm/encode.c \
	$(ENGINE_DIR)/wast/stream.c $(ENGINE_DIR)/wast/command.c \
	$(ENGINE_DIR)/store.c \
	$(ENGINE_DIR)/wast/runner.c $(ENGINE_DIR)/wast/assert.c \
	$(ENGINE_DIR)/wat/builder.c $(ENGINE_DIR)/wat/literal.c \
	$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
	$(ENGINE_DIR)/wasm/writer.c $(ENGINE_DIR)/wasm/module.c \
	$(ENGINE_DIR)/wasm/decode.c $(ENGINE_DIR)/op/validate.c \
	$(ENGINE_DIR)/instantiate.c $(ENGINE_DIR)/op/execute.c \
	$(ENGINE_DIR)/op/execute_memory.c \
	$(ENGINE_DIR)/op/execute_numeric.c \
	$(ENGINE_DIR)/op/execute_simd.c $(ENGINE_DIR)/op/execute_gc.c \
	$(ENGINE_DIR)/op/execute_exception.c \
	$(ENGINE_DIR)/op/execute_table.c \
	$(WAST_GEN_DIR)/dispatch_gen.c $(ENGINE_DIR)/instance.c \
	$(ENGINE_DIR)/api.c

ENGINE_HEADERS := $(ENGINE_DIR)/wat/types.h $(ENGINE_DIR)/store.h \
	$(ENGINE_DIR)/wasm/encode.h $(ENGINE_DIR)/engine_internal.h \
	$(ENGINE_DIR)/wast/runner.h $(ENGINE_DIR)/wast/stream.h \
	$(ENGINE_DIR)/wast/assert.h $(ENGINE_DIR)/wast/command.h \
	$(ENGINE_DIR)/wasm/opcode.h \
	$(ENGINE_DIR)/wat/context.h $(ENGINE_DIR)/wat/literal.h \
	$(ENGINE_DIR)/wat/builder.h \
	$(ENGINE_DIR)/wasm/leb.h \
	$(ENGINE_DIR)/wasm/reader.h $(ENGINE_DIR)/wasm/writer.h \
	$(ENGINE_DIR)/wasm/module.h $(ENGINE_DIR)/wasm/decode.h \
	$(ENGINE_DIR)/op/validate.h $(WAST_GEN_DIR)/dispatch_gen.h \
	$(ENGINE_DIR)/instantiate.h $(ENGINE_DIR)/runtime_internal.h

ENGINE_HEADERS += $(ENGINE_DIR)/include/waste.h

# Freestanding library: shared (portable) sources from engine
LIB_SHARED_SRC := $(ENGINE_DIR)/lib/string.c $(ENGINE_DIR)/lib/stdio.c \
	$(ENGINE_DIR)/lib/stdlib.c $(ENGINE_DIR)/lib/math.c $(ENGINE_DIR)/lib/errno.c

# Freestanding library: Wasm platform backend (local to html-rt)
LIB_WASM_SRC := lib/stdlib.c lib/stdio.c lib/unistd.c

# Object files
ENGINE_OBJS := $(BUILD_DIR)/store.o $(BUILD_DIR)/wasm_load.o \
	$(BUILD_DIR)/wast_runner.o $(BUILD_DIR)/wasm_encode.o \
	$(BUILD_DIR)/wast_stream.o $(BUILD_DIR)/wast_command.o \
	$(BUILD_DIR)/wast_assert.o \
	$(BUILD_DIR)/wasm_opcode.o \
	$(BUILD_DIR)/wat_builder.o $(BUILD_DIR)/wat_literal.o \
	$(BUILD_DIR)/wasm_leb.o $(BUILD_DIR)/wasm_reader.o \
	$(BUILD_DIR)/wasm_writer.o $(BUILD_DIR)/wasm_module.o \
	$(BUILD_DIR)/wasm_decode.o $(BUILD_DIR)/wasm_validate.o \
	$(BUILD_DIR)/instantiate.o $(BUILD_DIR)/execute.o \
	$(BUILD_DIR)/execute_memory.o $(BUILD_DIR)/execute_numeric.o \
	$(BUILD_DIR)/execute_simd.o $(BUILD_DIR)/execute_gc.o \
	$(BUILD_DIR)/execute_exception.o $(BUILD_DIR)/execute_table.o \
	$(BUILD_DIR)/dispatch_gen.o \
	$(BUILD_DIR)/instance.o $(BUILD_DIR)/api.o

RT_OBJS := $(BUILD_DIR)/browser_api.o $(BUILD_DIR)/posix_stubs.o

WASM_LIB_SHARED_O := $(patsubst $(ENGINE_DIR)/lib/%.c,$(BUILD_DIR)/lib_shared_%.o,$(LIB_SHARED_SRC))
WASM_LIB_WASM_O := $(patsubst lib/%.c,$(BUILD_DIR)/lib_wasm_%.o,$(LIB_WASM_SRC))

WAST_TAB_C := $(WAST_GEN_DIR)/wast.tab.c
WAST_TAB_H := $(WAST_GEN_DIR)/wast.tab.h
WAST_LEX_C := $(WAST_GEN_DIR)/wast.lex.c
WAST_TAB_WASM_O := $(BUILD_DIR)/wast.tab.wasm.o
WAST_LEX_WASM_O := $(BUILD_DIR)/wast.lex.wasm.o

WAST_WASM_TARGET := $(BUILD_DIR)/waste-wast.wasm

LIBC_BUILDER := tools/build-waste-libc.py
LIBC_CLIENTS := $(wildcard $(REPO_ROOT)/tests/libc-test/*-client.wast.inc)
LIBC_SOURCES := lib/stdlib.wat lib/stdio.c lib/wchar.c \
	lib/locale.c lib/identity.c lib/string.c \
	lib/stdlib.c lib/pattern.c lib/misc.c \
	lib/time.c lib/termcap.c lib/termios.c \
	lib/dirent.c lib/netdb.c lib/dlfcn.c lib/unistd.c \
	lib/sys/random.c lib/sys/resource.c lib/sys/select.c \
	lib/sys/ioctl.c lib/sys/socket.c \
	lib/include/helper.h $(LIBC_CLIENTS) $(LIBC_BUILDER)
LIBC_OUTPUT := $(BUILD_DIR)/waste-libc/waste-libc.wasm
LIBC_FIXTURES := $(patsubst $(REPO_ROOT)/tests/libc-test/%-client.wast.inc,\
	$(BUILD_DIR)/waste-libc/tests/%.wast,$(LIBC_CLIENTS))
LIBC_OUTPUTS := $(LIBC_OUTPUT) $(LIBC_FIXTURES)

.PHONY: wast-browser waste-libc gen clean

# ---- Parser generation (delegate to engine) ----

gen:
	$(MAKE) -C $(ENGINE_DIR) BUILD_DIR=$(abspath $(ENGINE_BUILD_DIR)) gen

# ---- Compile engine sources to Wasm objects ----

$(BUILD_DIR)/store.o: $(ENGINE_DIR)/store.c $(ENGINE_HEADERS)
	mkdir -p $(BUILD_DIR)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wasm_load.o: $(ENGINE_DIR)/wasm/load.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wast_runner.o: $(ENGINE_DIR)/wast/runner.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wasm_encode.o: $(ENGINE_DIR)/wasm/encode.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wast_stream.o: $(ENGINE_DIR)/wast/stream.c $(ENGINE_HEADERS) $(WAST_TAB_H)
	clang $(WASM_CC_FLAGS) -I$(WAST_GEN_DIR) -c $< -o $@

$(BUILD_DIR)/wast_command.o: $(ENGINE_DIR)/wast/command.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wast_assert.o: $(ENGINE_DIR)/wast/assert.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wasm_opcode.o: $(ENGINE_DIR)/wasm/opcode.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wat_builder.o: $(ENGINE_DIR)/wat/builder.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wat_literal.o: $(ENGINE_DIR)/wat/literal.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wasm_leb.o: $(ENGINE_DIR)/wasm/leb.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wasm_reader.o: $(ENGINE_DIR)/wasm/reader.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wasm_writer.o: $(ENGINE_DIR)/wasm/writer.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wasm_module.o: $(ENGINE_DIR)/wasm/module.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wasm_decode.o: $(ENGINE_DIR)/wasm/decode.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wasm_validate.o: $(ENGINE_DIR)/op/validate.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/instantiate.o: $(ENGINE_DIR)/instantiate.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/execute.o: $(ENGINE_DIR)/op/execute.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/execute_memory.o: $(ENGINE_DIR)/op/execute_memory.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/execute_numeric.o: $(ENGINE_DIR)/op/execute_numeric.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/execute_simd.o: $(ENGINE_DIR)/op/execute_simd.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/execute_gc.o: $(ENGINE_DIR)/op/execute_gc.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/execute_exception.o: $(ENGINE_DIR)/op/execute_exception.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/execute_table.o: $(ENGINE_DIR)/op/execute_table.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/dispatch_gen.o: $(WAST_GEN_DIR)/dispatch_gen.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/api.o: $(ENGINE_DIR)/api.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/instance.o: $(ENGINE_DIR)/instance.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

# ---- Compile html-rt sources to Wasm objects ----

$(BUILD_DIR)/browser_api.o: browser_api.c $(ENGINE_HEADERS) posix_stubs.h
	clang $(WASM_CC_FLAGS) -I. -c $< -o $@

$(BUILD_DIR)/posix_stubs.o: posix_stubs.c posix_stubs.h $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -I. -c $< -o $@

# ---- Compile freestanding library objects ----

$(BUILD_DIR)/lib_shared_%.o: $(ENGINE_DIR)/lib/%.c
	mkdir -p $(BUILD_DIR)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/lib_wasm_%.o: lib/%.c
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
