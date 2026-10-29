# Independent binary-command profile: no WAT/WAST parser or browser shell API.
COMMAND_BUILD := $(BUILD_DIR)/command
COMMAND_ENGINE_SOURCES := process_driver.c guest_posix.c store.c checkpoint.c process.c source.c \
	session/session.c lib/kernel.c lib/select.c lib/path.c wasm/load.c wasm/opcode.c \
	wasm/leb.c wasm/reader.c wasm/module.c wasm/decode.c instantiate.c instance.c \
	op/validate.c op/execute.c op/execute_memory.c op/execute_numeric.c op/execute_simd.c \
	op/execute_gc.c op/execute_exception.c op/execute_table.c
COMMAND_SUPPORT_SOURCES := string.c stdio.c stdlib.c math.c errno.c
COMMAND_OBJECTS := $(addprefix $(COMMAND_BUILD)/engine/,$(COMMAND_ENGINE_SOURCES:.c=.o)) \
	$(addprefix $(COMMAND_BUILD)/support/,$(COMMAND_SUPPORT_SOURCES:.c=.o)) \
	$(COMMAND_BUILD)/platform/stdlib.o $(COMMAND_BUILD)/platform/stdio.o \
	$(COMMAND_BUILD)/dispatch.o $(COMMAND_BUILD)/command_api.o
COMMAND_FLAGS := $(WASM_CC_FLAGS) -DWASTE_BINARY_COMMAND -ffunction-sections -fdata-sections
COMMAND_HEADERS := $(ENGINE_HEADERS) $(ENGINE_DIR)/process_driver.h $(ENGINE_DIR)/include/waste.h \
	$(ENGINE_DIR)/guest_posix.h $(wildcard $(ENGINE_DIR)/lib/include/*.h)

$(COMMAND_OBJECTS): Makefile command/command.mk

.PHONY: embedding
embedding: $(BUILD_DIR)/wasm.wasm $(BUILD_DIR)/waste.mjs $(BUILD_DIR)/waste.worker.mjs

EMBEDDING_TOOLS ?= awk,m4,dot
EMBEDDING_SHELL ?= 0
EMBEDDING_RELEASE ?= development
EMBEDDING_DIR ?= $(BUILD_DIR)/embedding
.PHONY: embedding-package
embedding-package: embedding
	node tools/package-embedding.mjs --build=$(abspath $(BUILD_DIR)) \
		--output=$(abspath $(EMBEDDING_DIR)) --tools=$(EMBEDDING_TOOLS) --with-shell=$(EMBEDDING_SHELL) "--release=$(EMBEDDING_RELEASE)"

# Binary execution only needs opcode generation. Do not make a fresh embedding
# build depend on the full parser-generating runtime goal.
$(WAST_GEN_DIR)/dispatch_gen.c $(WAST_GEN_DIR)/dispatch_gen.h &: $(ENGINE_DIR)/op/opcodes.tab $(ENGINE_DIR)/op/gen-dispatch.awk
	$(MAKE) -C $(ENGINE_DIR) BUILD_DIR=$(abspath $(ENGINE_BUILD_DIR)) $(abspath $(WAST_GEN_DIR)/dispatch_gen.c)

$(BUILD_DIR)/waste.mjs: command/waste.mjs
	mkdir -p $(@D)
	cp $< $@

$(BUILD_DIR)/waste.worker.mjs: command/waste.worker.mjs
	mkdir -p $(@D)
	cp $< $@

$(COMMAND_BUILD)/engine/%.o: $(ENGINE_DIR)/%.c $(COMMAND_HEADERS)
	mkdir -p $(@D)
	clang $(COMMAND_FLAGS) -c $< -o $@
$(COMMAND_BUILD)/support/%.o: ../aux/libc/runtime/%.c $(COMMAND_HEADERS)
	mkdir -p $(@D)
	clang $(COMMAND_FLAGS) -c $< -o $@
$(COMMAND_BUILD)/platform/%.o: lib/%.c $(COMMAND_HEADERS)
	mkdir -p $(@D)
	clang $(COMMAND_FLAGS) -c $< -o $@
$(COMMAND_BUILD)/command_api.o: command/command_api.c $(COMMAND_HEADERS)
	mkdir -p $(@D)
	clang $(COMMAND_FLAGS) -c $< -o $@
$(COMMAND_BUILD)/dispatch.o: $(WAST_GEN_DIR)/dispatch_gen.c $(WAST_GEN_DIR)/dispatch_gen.h
	mkdir -p $(@D)
	clang $(COMMAND_FLAGS) -c $< -o $@

$(BUILD_DIR)/wasm.wasm: $(COMMAND_OBJECTS)
	env LD_LIBRARY_PATH=$(WASM_LD_LIB) $(WASM_LD) --no-entry --export-memory --gc-sections \
		--initial-memory=16777216 --max-memory=$(HTML_RUNTIME_MAX_MEMORY) -z stack-size=4194304 \
		$^ -o $@
