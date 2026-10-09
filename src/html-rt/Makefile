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
	$(ENGINE_DIR)/vfs.c \
	$(ENGINE_DIR)/source.c \
	$(ENGINE_DIR)/wasm/opcode.c $(ENGINE_DIR)/wasm/encode.c \
	$(ENGINE_DIR)/wast/stream.c $(ENGINE_DIR)/wast/command.c \
	$(ENGINE_DIR)/store.c $(ENGINE_DIR)/checkpoint.c $(ENGINE_DIR)/process.c $(ENGINE_DIR)/lib/path.c \
	$(ENGINE_DIR)/wast/runner.c $(ENGINE_DIR)/wast/assert.c $(ENGINE_DIR)/wast/handler.c \
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

ENGINE_HEADERS := $(ENGINE_DIR)/lib/json.h $(ENGINE_DIR)/wat/types.h $(ENGINE_DIR)/store.h \
	$(ENGINE_DIR)/source.h \
	$(ENGINE_DIR)/wasm/encode.h $(ENGINE_DIR)/engine_internal.h \
	$(ENGINE_DIR)/wast/runner.h $(ENGINE_DIR)/wast/stream.h \
	$(ENGINE_DIR)/wast/assert.h $(ENGINE_DIR)/wast/command.h $(ENGINE_DIR)/wast/handler.h \
	$(ENGINE_DIR)/wasm/opcode.h \
	$(ENGINE_DIR)/wat/context.h $(ENGINE_DIR)/wat/literal.h \
	$(ENGINE_DIR)/wat/builder.h \
	$(ENGINE_DIR)/wasm/leb.h \
	$(ENGINE_DIR)/wasm/reader.h $(ENGINE_DIR)/wasm/writer.h \
	$(ENGINE_DIR)/wasm/module.h $(ENGINE_DIR)/wasm/decode.h \
	$(ENGINE_DIR)/op/validate.h $(WAST_GEN_DIR)/dispatch_gen.h \
	$(ENGINE_DIR)/instantiate.h $(ENGINE_DIR)/runtime_internal.h

ENGINE_HEADERS += $(ENGINE_DIR)/process_driver.h $(ENGINE_DIR)/vfs.h $(ENGINE_DIR)/../config.h $(ENGINE_DIR)/guest_posix.h $(ENGINE_DIR)/include/waste.h
ENGINE_HEADERS += $(ENGINE_DIR)/wast/setup.h
ENGINE_HEADERS += $(ENGINE_DIR)/lib/include/kernel.h \
	$(ENGINE_DIR)/lib/include/select.h $(ENGINE_DIR)/lib/include/path.h

# Freestanding library: shared portable interpreter support
LIB_SHARED_SRC := ../aux/libc/runtime/string.c ../aux/libc/runtime/stdio.c \
	../aux/libc/runtime/stdlib.c ../aux/libc/runtime/math.c ../aux/libc/runtime/errno.c

# Freestanding library: Wasm platform backend (local to html-rt)
LIB_WASM_SRC := lib/stdlib.c lib/stdio.c lib/unistd.c

# Object files
ENGINE_OBJS := $(BUILD_DIR)/process_driver.o $(BUILD_DIR)/guest_posix.o $(BUILD_DIR)/store.o $(BUILD_DIR)/checkpoint.o $(BUILD_DIR)/process.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/select.o $(BUILD_DIR)/path.o \
	$(BUILD_DIR)/source.o $(BUILD_DIR)/vfs.o \
	$(BUILD_DIR)/wasm_load.o \
	$(BUILD_DIR)/wast_runner.o $(BUILD_DIR)/wasm_encode.o \
	$(BUILD_DIR)/wast_stream.o $(BUILD_DIR)/wast_command.o \
	$(BUILD_DIR)/wast_assert.o $(BUILD_DIR)/wast_handler.o \
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

RT_OBJS := $(BUILD_DIR)/browser_api.o $(BUILD_DIR)/posix_stubs.o $(BUILD_DIR)/test_suite.o

ENGINE_HEADERS += $(ENGINE_DIR)/test_suite.h

WASM_LIB_SHARED_O := $(patsubst ../aux/libc/runtime/%.c,$(BUILD_DIR)/lib_shared_%.o,$(LIB_SHARED_SRC))
WASM_LIB_WASM_O := $(patsubst lib/%.c,$(BUILD_DIR)/lib_wasm_%.o,$(LIB_WASM_SRC))

WAST_TAB_C := $(WAST_GEN_DIR)/wast.tab.c
WAST_TAB_H := $(WAST_GEN_DIR)/wast.tab.h
WAST_LEX_C := $(WAST_GEN_DIR)/wast.lex.c
WAST_TAB_WASM_O := $(BUILD_DIR)/wast.tab.wasm.o
WAST_LEX_WASM_O := $(BUILD_DIR)/wast.lex.wasm.o

WAST_WASM_TARGET := $(BUILD_DIR)/waste-wast.wasm

.PHONY: wast-browser gen clean coreutils-sysroot coreutils-crt-fixture

# ---- Parser generation (delegate to engine) ----

gen:
	$(MAKE) -C $(ENGINE_DIR) BUILD_DIR=$(abspath $(ENGINE_BUILD_DIR)) gen

# ---- Compile engine sources to Wasm objects ----

$(BUILD_DIR)/process_driver.o: $(ENGINE_DIR)/process_driver.c $(ENGINE_HEADERS)
	mkdir -p $(BUILD_DIR)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/store.o: $(ENGINE_DIR)/store.c $(ENGINE_HEADERS)
	mkdir -p $(BUILD_DIR)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/checkpoint.o: $(ENGINE_DIR)/checkpoint.c $(ENGINE_HEADERS)
	mkdir -p $(BUILD_DIR)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/process.o: $(ENGINE_DIR)/process.c $(ENGINE_HEADERS)
	mkdir -p $(BUILD_DIR)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/kernel.o: $(ENGINE_DIR)/lib/kernel.c $(ENGINE_HEADERS) \
		$(ENGINE_DIR)/lib/include/kernel.h $(ENGINE_DIR)/lib/include/select.h
	mkdir -p $(BUILD_DIR)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/select.o: $(ENGINE_DIR)/lib/select.c \
		$(ENGINE_DIR)/lib/include/select.h
	mkdir -p $(BUILD_DIR)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/path.o: $(ENGINE_DIR)/lib/path.c \
		$(ENGINE_DIR)/lib/include/path.h
	mkdir -p $(BUILD_DIR)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/wasm_load.o: $(ENGINE_DIR)/wasm/load.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/source.o: $(ENGINE_DIR)/source.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/vfs.o: $(ENGINE_DIR)/vfs.c $(ENGINE_HEADERS)
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

$(BUILD_DIR)/wast_handler.o: $(ENGINE_DIR)/wast/handler.c $(ENGINE_HEADERS)
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

$(BUILD_DIR)/test_suite.o: $(ENGINE_DIR)/test_suite.c $(ENGINE_DIR)/test_suite.h $(ENGINE_DIR)/lib/json.h
	mkdir -p $(BUILD_DIR)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/browser_api.o: browser_api.c $(ENGINE_HEADERS) posix_stubs.h
	clang $(WASM_CC_FLAGS) -I. -c $< -o $@

$(BUILD_DIR)/posix_stubs.o: posix_stubs.c posix_stubs.h $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -I. -c $< -o $@

$(BUILD_DIR)/guest_posix.o: $(ENGINE_DIR)/guest_posix.c $(ENGINE_HEADERS)
	clang $(WASM_CC_FLAGS) -c $< -o $@

# ---- Compile freestanding library objects ----

$(BUILD_DIR)/lib_shared_%.o: ../aux/libc/runtime/%.c
	mkdir -p $(BUILD_DIR)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/lib_wasm_%.o: lib/%.c
	mkdir -p $(BUILD_DIR)
	clang $(WASM_CC_FLAGS) -I$(REPO_ROOT)/src/vfs/usr/include -c $< -o $@

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

# Same private ownership/startup probe against the browser C object set.
.PHONY: direct-start-probe
direct-start-probe: gen $(BUILD_DIR)/direct-start-check.wasm

$(BUILD_DIR)/direct-start-check.o: ../system-tests/cli-runtime/direct-start.c $(ENGINE_HEADERS)
	mkdir -p $(BUILD_DIR)
	clang $(WASM_CC_FLAGS) -c $< -o $@

$(BUILD_DIR)/direct-start-check.wasm: $(BUILD_DIR)/direct-start-check.o $(ENGINE_OBJS) \
		$(WASM_LIB_SHARED_O) $(WASM_LIB_WASM_O) $(WAST_TAB_WASM_O) $(WAST_LEX_WASM_O)
	env LD_LIBRARY_PATH=$(WASM_LD_LIB) $(WASM_LD) --no-entry --export-memory \
		--export=malloc --export=free --initial-memory=67108864 --max-memory=536870912 \
		-z stack-size=8388608 $^ -o $@

$(WAST_WASM_TARGET): $(ENGINE_OBJS) $(RT_OBJS) $(WASM_LIB_SHARED_O) $(WASM_LIB_WASM_O) \
		$(WAST_TAB_WASM_O) $(WAST_LEX_WASM_O)
	env LD_LIBRARY_PATH=$(WASM_LD_LIB) $(WASM_LD) --no-entry --export-memory \
		--initial-memory=67108864 --max-memory=536870912 -z stack-size=8388608 \
		$(WASM_LIB_SHARED_O) $(WASM_LIB_WASM_O) \
		$(ENGINE_OBJS) $(RT_OBJS) \
		$(WAST_TAB_WASM_O) $(WAST_LEX_WASM_O) \
		-o $(WAST_WASM_TARGET)

# Guest libc and fixture generation are owned by the aux package.
.PHONY: libc-shared libc-shared-install
libc-shared:
	$(MAKE) -C ../aux libc

libc-shared-install:
	$(MAKE) -C ../aux install-libc

# Guest package ownership lives in src/aux; keep aggregate compatibility targets.
AUX_BUILD_DIR ?= $(REPO_ROOT)/build/aux
.PHONY: coreutils-stage build-coreutils coreutils-wasm coreutils-install coreutils-source-package
coreutils-stage build-coreutils coreutils-wasm coreutils-install coreutils-source-package:
	$(MAKE) -C ../aux BUILD_DIR="$(abspath $(AUX_BUILD_DIR))" \
		$(if $(filter coreutils-stage coreutils-source-package,$@),$@,$(if $(filter coreutils-install,$@),install-coreutils,coreutils))

# Compiler/SDK system probes are owned by src/system-tests.
coreutils-sysroot:
	$(MAKE) -C ../system-tests sysroot

coreutils-crt-fixture:
	$(MAKE) -C ../system-tests crt-fixture

.PHONY: vfs-audit vfs-install-launch vfs-install-app vfs-tests-install vfs-tests-check
vfs-tests-install: wast-browser
	$(PYTHON) tools/build-test-corpus.py --install

vfs-tests-check:
	$(PYTHON) $(REPO_ROOT)/tests/runtime-config-check.py
	$(PYTHON) tools/build-test-corpus.py --audit
	$(PYTHON) $(REPO_ROOT)/tests/test-corpus-check.py

vfs-audit:
	$(PYTHON) tools/vfs.py audit

.PHONY: guest-sdk-install guest-sdk-check
guest-sdk-install:
	$(PYTHON) tools/build-guest-sdk.py --install

guest-sdk-check:
	$(PYTHON) tools/build-guest-sdk.py --audit
	$(PYTHON) $(REPO_ROOT)/src/system-tests/guest-sdk/check.py
	$(PYTHON) tools/audit-guest-providers.py


vfs-install-launch:
	$(MAKE) -C ../aux install-bash-launch

vfs-install-app:
	$(PYTHON) tools/vfs.py install --component app --source $(REPO_ROOT)/src/html-rt/src

clean:
	rm -f $(WAST_WASM_TARGET) $(ENGINE_OBJS) $(RT_OBJS) \
		$(WASM_LIB_SHARED_O) $(WASM_LIB_WASM_O) \
		$(WAST_TAB_WASM_O) $(WAST_LEX_WASM_O)

$(BUILD_DIR)/path.o $(BUILD_DIR)/test_suite.o: ../config.h

# Package current guest files without rebuilding or reinstalling guest programs.
.PHONY: bash-html
bash-html: wast-browser
	bash tools/build.sh bash --wasm "$(abspath $(WAST_WASM_TARGET))" --output "$(abspath $(BUILD_DIR))/bash.html"
