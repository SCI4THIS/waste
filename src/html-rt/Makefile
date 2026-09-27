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
	$(ENGINE_DIR)/source.c \
	$(ENGINE_DIR)/wasm/opcode.c $(ENGINE_DIR)/wasm/encode.c \
	$(ENGINE_DIR)/wast/stream.c $(ENGINE_DIR)/wast/command.c \
	$(ENGINE_DIR)/store.c $(ENGINE_DIR)/checkpoint.c $(ENGINE_DIR)/process.c $(ENGINE_DIR)/lib/path.c \
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
	$(ENGINE_DIR)/source.h \
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
ENGINE_HEADERS += $(ENGINE_DIR)/lib/include/kernel.h \
	$(ENGINE_DIR)/lib/include/select.h

# Freestanding library: shared (portable) sources from engine
LIB_SHARED_SRC := $(ENGINE_DIR)/lib/string.c $(ENGINE_DIR)/lib/stdio.c \
	$(ENGINE_DIR)/lib/stdlib.c $(ENGINE_DIR)/lib/math.c $(ENGINE_DIR)/lib/errno.c

# Freestanding library: Wasm platform backend (local to html-rt)
LIB_WASM_SRC := lib/stdlib.c lib/stdio.c lib/unistd.c

# Object files
ENGINE_OBJS := $(BUILD_DIR)/store.o $(BUILD_DIR)/checkpoint.o $(BUILD_DIR)/process.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/select.o $(BUILD_DIR)/path.o \
	$(BUILD_DIR)/source.o \
	$(BUILD_DIR)/wasm_load.o \
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
LIBC_SOURCE_MANIFEST := tools/libc_sources.py
LIBC_CLIENTS := $(wildcard $(REPO_ROOT)/tests/libc-test/*-client.wast.inc)
LIBC_SOURCES := lib/stdlib.wat lib/stdio.c lib/wchar.c \
	lib/locale.c lib/identity.c lib/string.c \
	lib/math.c lib/quad.c \
	lib/stdlib.c lib/pattern.c lib/misc.c \
	lib/time.c lib/termcap.c lib/termios.c \
	lib/dirent.c lib/netdb.c lib/dlfcn.c lib/unistd.c \
	lib/sys/random.c lib/sys/resource.c lib/sys/select.c \
	lib/sys/ioctl.c lib/sys/socket.c \
	lib/include/helper.h $(LIBC_CLIENTS) $(LIBC_BUILDER) $(LIBC_SOURCE_MANIFEST)
LIBC_OUTPUT := $(BUILD_DIR)/waste-libc/waste-libc.wasm
LIBC_FIXTURES := $(patsubst $(REPO_ROOT)/tests/libc-test/%-client.wast.inc,\
	$(BUILD_DIR)/waste-libc/tests/%.wast,$(LIBC_CLIENTS))
LIBC_OUTPUTS := $(LIBC_OUTPUT) $(LIBC_FIXTURES)

COREUTILS_STAGER := tools/stage-coreutils.py
COREUTILS_BUILD_DIR ?= $(REPO_ROOT)/build/coreutils
COREUTILS_PATCH := $(REPO_ROOT)/submodules/coreutils-waste.patch
COREUTILS_STAGE_STAMP := $(COREUTILS_BUILD_DIR)/source/.waste-stage-stamp
WASTE_SYSROOT_TOOL := tools/build-waste-sysroot.py
WASTE_SYSROOT_DIR := $(COREUTILS_BUILD_DIR)/sysroot
WASTE_SYSROOT_CC := $(WASTE_SYSROOT_DIR)/bin/waste-wasm-clang
WASTE_SYSROOT_HEADERS := $(shell find $(REPO_ROOT)/src/html-rt/lib/include \
	-type f -name '*.h' -print)
COREUTILS_CONFIGURE_TOOL := tools/configure-coreutils.py
COREUTILS_CONFIGURE_DIR := $(COREUTILS_BUILD_DIR)/configure
COREUTILS_CONFIGURE_REPORT := $(COREUTILS_CONFIGURE_DIR)/configure-report.json
COREUTILS_CONFIGURE_SCRIPT := $(COREUTILS_BUILD_DIR)/source/configure
COREUTILS_UTILITY_TOOL := tools/probe-coreutils-utility.py
COREUTILS_RUNTIME_TOOL := tools/build-coreutils-runtime.py
COREUTILS_PACKAGE_AUDIT := tools/audit-coreutils-package.py
COREUTILS_SOURCE_PACKAGER := tools/package-coreutils-source.py
COREUTILS_SOURCE_ARCHIVE := $(COREUTILS_BUILD_DIR)/coreutils-corresponding-source.tar.gz
COREUTILS_SOURCE_MAPPING := $(COREUTILS_BUILD_DIR)/coreutils-source-package.json
COREUTILS_SOURCE_STAMP := $(COREUTILS_BUILD_DIR)/.source-package-stamp
COREUTILS_SOURCE_INPUTS := $(shell find $(REPO_ROOT)/src/engine $(REPO_ROOT)/src/html-rt \
	-type f ! -name '*.wasm' ! -name '*.pyc' ! -path '*/__pycache__/*' -print)
COREUTILS_UTILITY_DIR := $(COREUTILS_BUILD_DIR)/utility-probe
COREUTILS_UTILITY_REPORT := $(COREUTILS_UTILITY_DIR)/true-report.json
COREUTILS_FALSE_REPORT := $(COREUTILS_UTILITY_DIR)/false-report.json
COREUTILS_PWD_REPORT := $(COREUTILS_UTILITY_DIR)/pwd-report.json
COREUTILS_ECHO_REPORT := $(COREUTILS_UTILITY_DIR)/echo-report.json
COREUTILS_PRINTF_REPORT := $(COREUTILS_UTILITY_DIR)/printf-report.json
COREUTILS_BASENAME_REPORT := $(COREUTILS_UTILITY_DIR)/basename-report.json
COREUTILS_DIRNAME_REPORT := $(COREUTILS_UTILITY_DIR)/dirname-report.json
COREUTILS_CAT_REPORT := $(COREUTILS_UTILITY_DIR)/cat-report.json
COREUTILS_WC_REPORT := $(COREUTILS_UTILITY_DIR)/wc-report.json
COREUTILS_LS_REPORT := $(COREUTILS_UTILITY_DIR)/ls-report.json
COREUTILS_BOOTSTRAP := $(REPO_ROOT)/submodules/bootstrap-coreutils.sh
WASTE_CRT_SOURCE := lib/waste-crt.c
WASTE_CRT_OBJECT := $(COREUTILS_BUILD_DIR)/waste-crt.o
WASTE_HELLO_SOURCE := $(REPO_ROOT)/tests/coreutils-sysroot-hello.c
WASTE_HELLO_OBJECT := $(COREUTILS_BUILD_DIR)/sysroot-hello.o
WASTE_HELLO_WASM := $(COREUTILS_BUILD_DIR)/sysroot-hello.wasm
WASTE_HELLO_WAT := $(COREUTILS_BUILD_DIR)/sysroot-hello.wat
WASTE_HELLO_AUDIT := $(COREUTILS_BUILD_DIR)/sysroot-hello.audit.json

.PHONY: wast-browser waste-libc coreutils-patch-status \
	coreutils-provenance coreutils-sysroot \
coreutils-crt-fixture coreutils-configure-probe coreutils-utility-probe \
	coreutils-true-probe coreutils-false-probe coreutils-pwd-probe \
	coreutils-echo-probe coreutils-printf-probe coreutils-basename-probe \
	coreutils-dirname-probe coreutils-cat-probe coreutils-wc-probe coreutils-ls-probe \
	build-coreutils coreutils-wasm coreutils-audit \
	coreutils-package-audit coreutils-source-package \
	gen clean

# ---- Parser generation (delegate to engine) ----

gen:
	$(MAKE) -C $(ENGINE_DIR) BUILD_DIR=$(abspath $(ENGINE_BUILD_DIR)) gen

# ---- Compile engine sources to Wasm objects ----

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
		--initial-memory=67108864 --max-memory=536870912 -z stack-size=8388608 \
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

# Stage 8A: inspect and stage the pinned coreutils source. The stager applies
# the repository-owned patch only in the submodule, copies the patched source
# into build/, and restores the submodule before it exits.
coreutils-patch-status:
	$(PYTHON) $(COREUTILS_STAGER) --repo-root $(abspath $(REPO_ROOT)) --action status

coreutils-provenance:
	$(PYTHON) $(COREUTILS_STAGER) --repo-root $(abspath $(REPO_ROOT)) \
		--action provenance --output $(COREUTILS_BUILD_DIR)/provenance.json

coreutils-stage: $(COREUTILS_STAGE_STAMP)

$(COREUTILS_STAGE_STAMP): $(COREUTILS_STAGER) $(COREUTILS_PATCH)
	$(PYTHON) $(COREUTILS_STAGER) --repo-root $(abspath $(REPO_ROOT)) \
		--action stage --output $(COREUTILS_BUILD_DIR)
	touch $@

$(COREUTILS_CONFIGURE_SCRIPT): $(COREUTILS_STAGE_STAMP) $(COREUTILS_BOOTSTRAP)
	WASTE_COREUTILS_BUILD_DIR=$(abspath $(COREUTILS_BUILD_DIR)) \
		$(COREUTILS_BOOTSTRAP) --no-install

# Stage 8D: generate the freestanding target sysroot and build the startup
# fixture through its compiler wrapper.  All outputs remain under build/.
coreutils-sysroot: $(WASTE_SYSROOT_DIR)/manifest.json

$(WASTE_SYSROOT_DIR)/manifest.json: $(WASTE_SYSROOT_TOOL) $(WASTE_CRT_SOURCE) \
		$(WASTE_SYSROOT_HEADERS)
	mkdir -p $(COREUTILS_BUILD_DIR)
	$(PYTHON) $(WASTE_SYSROOT_TOOL) --repo-root $(abspath $(REPO_ROOT)) \
		--output $(abspath $(WASTE_SYSROOT_DIR))

$(WASTE_CRT_OBJECT): $(WASTE_SYSROOT_DIR)/manifest.json $(WASTE_CRT_SOURCE)
	mkdir -p $(COREUTILS_BUILD_DIR)
	$(WASTE_SYSROOT_CC) -c $(WASTE_CRT_SOURCE) -o $@

$(WASTE_HELLO_OBJECT): $(WASTE_SYSROOT_DIR)/manifest.json $(WASTE_HELLO_SOURCE)
	mkdir -p $(COREUTILS_BUILD_DIR)
	$(WASTE_SYSROOT_CC) -c $(WASTE_HELLO_SOURCE) -o $@

$(WASTE_HELLO_WASM): $(WASTE_CRT_OBJECT) $(WASTE_HELLO_OBJECT)
	env LD_LIBRARY_PATH=$(WASM_LD_LIB) $(WASM_LD) --no-entry --gc-sections \
		--export=_start --export-memory --initial-memory=33554432 \
		--max-memory=268435456 $(WASTE_CRT_OBJECT) $(WASTE_HELLO_OBJECT) -o $@

$(WASTE_HELLO_WAT): $(WASTE_HELLO_WASM)
	wasm-dis $< -o $@

$(WASTE_HELLO_AUDIT): $(WASTE_HELLO_WASM)
	$(PYTHON) tools/audit-wasm-imports.py $< \
		--allow env:exit --allow env:write \
		--allow waste_kernel:startup_v1 --json $@

coreutils-crt-fixture: $(WASTE_HELLO_AUDIT) $(WASTE_HELLO_WAT)
	$(PYTHON) -c 'import json; p=json.load(open("$(WASTE_HELLO_AUDIT)")); assert p["ok"] and not p["unknown"] and not p["asyncifySymbols"]'

coreutils-configure-probe: $(COREUTILS_CONFIGURE_REPORT)

$(COREUTILS_CONFIGURE_REPORT): $(COREUTILS_CONFIGURE_TOOL) \
		tools/coreutils-waste.config.site $(WASTE_SYSROOT_DIR)/manifest.json \
		$(COREUTILS_CONFIGURE_SCRIPT)
	$(PYTHON) $(COREUTILS_CONFIGURE_TOOL) \
		--repo-root $(abspath $(REPO_ROOT)) \
		--source $(abspath $(COREUTILS_BUILD_DIR)/source) \
		--sysroot $(abspath $(WASTE_SYSROOT_DIR)) \
		--output $(abspath $(COREUTILS_CONFIGURE_DIR))

# Compile the first deliberately small upstream utility. A missing guest
# header is an expected, machine-readable handoff to Stage 8E; an unexpected
# compiler or linker failure remains a failed target.
coreutils-utility-probe: $(COREUTILS_UTILITY_REPORT)
coreutils-true-probe: $(COREUTILS_UTILITY_REPORT)
coreutils-false-probe: $(COREUTILS_FALSE_REPORT)
coreutils-pwd-probe: $(COREUTILS_PWD_REPORT)
coreutils-echo-probe: $(COREUTILS_ECHO_REPORT)
coreutils-printf-probe: $(COREUTILS_PRINTF_REPORT)
coreutils-basename-probe: $(COREUTILS_BASENAME_REPORT)
coreutils-dirname-probe: $(COREUTILS_DIRNAME_REPORT)
coreutils-cat-probe: $(COREUTILS_CAT_REPORT)
coreutils-wc-probe: $(COREUTILS_WC_REPORT)
coreutils-ls-probe: $(COREUTILS_LS_REPORT)

# Public Stage 8F entry point. All staged sources, sysroot files, configure
# output, objects, reports, and linked utilities live below build/coreutils.
COREUTILS_REPORTS := $(COREUTILS_UTILITY_REPORT) $(COREUTILS_FALSE_REPORT) \
	$(COREUTILS_PWD_REPORT) $(COREUTILS_ECHO_REPORT) \
	$(COREUTILS_PRINTF_REPORT) $(COREUTILS_BASENAME_REPORT) \
	$(COREUTILS_DIRNAME_REPORT) $(COREUTILS_CAT_REPORT) $(COREUTILS_WC_REPORT) \
	$(COREUTILS_LS_REPORT)

build-coreutils: coreutils-provenance coreutils-utility-probe coreutils-false-probe \
		coreutils-pwd-probe coreutils-echo-probe coreutils-printf-probe \
		coreutils-basename-probe coreutils-dirname-probe coreutils-cat-probe \
		coreutils-wc-probe coreutils-ls-probe
	$(PYTHON) -c 'import json; reports=[json.load(open(p)) for p in "$(COREUTILS_REPORTS)".split()]; assert all(p["status"] == "passed" for p in reports), "coreutils utility gate failed: " + "; ".join(p["utility"] + ": " + "; ".join(p["blockers"]) for p in reports if p["status"] != "passed")'

# Stable names for the sectioned build entry points documented in Stage 8H.
coreutils-wasm: build-coreutils
coreutils-audit: $(COREUTILS_REPORTS)
	$(PYTHON) -c 'import json; reports=[json.load(open(p)) for p in "$(COREUTILS_REPORTS)".split()]; assert all(p["status"] == "passed" for p in reports), "coreutils audit failed"'

coreutils-source-package: $(COREUTILS_SOURCE_STAMP)

$(COREUTILS_SOURCE_STAMP): $(COREUTILS_SOURCE_PACKAGER) $(COREUTILS_CONFIGURE_SCRIPT) \
		$(COREUTILS_BUILD_DIR)/provenance.json \
		$(COREUTILS_PATCH) $(REPO_ROOT)/start.sh \
		$(REPO_ROOT)/docs/coreutils-source-distribution.md \
		$(COREUTILS_SOURCE_INPUTS)
	$(PYTHON) $(COREUTILS_SOURCE_PACKAGER) \
		--repo-root $(abspath $(REPO_ROOT)) \
		--source $(abspath $(COREUTILS_BUILD_DIR)/source) \
		--provenance $(abspath $(COREUTILS_BUILD_DIR)/provenance.json) \
		--output $(abspath $(COREUTILS_SOURCE_ARCHIVE)) \
		--mapping $(abspath $(COREUTILS_SOURCE_MAPPING))
	touch $@

coreutils-package-audit: build-coreutils coreutils-source-package $(COREUTILS_PACKAGE_AUDIT)
	bash tools/build.sh bash

$(COREUTILS_UTILITY_REPORT): $(COREUTILS_UTILITY_TOOL) $(COREUTILS_RUNTIME_TOOL) \
		$(COREUTILS_CONFIGURE_REPORT) $(LIBC_OUTPUT)
	mkdir -p $(COREUTILS_UTILITY_DIR)
	$(PYTHON) $(COREUTILS_UTILITY_TOOL) \
		--repo-root $(abspath $(REPO_ROOT)) \
		--build $(abspath $(COREUTILS_CONFIGURE_DIR)) \
		--utility true \
		--output $@

$(COREUTILS_FALSE_REPORT): $(COREUTILS_UTILITY_TOOL) $(COREUTILS_RUNTIME_TOOL) \
		$(COREUTILS_CONFIGURE_REPORT) $(LIBC_OUTPUT)
	mkdir -p $(COREUTILS_UTILITY_DIR)
	$(PYTHON) $(COREUTILS_UTILITY_TOOL) \
		--repo-root $(abspath $(REPO_ROOT)) \
		--build $(abspath $(COREUTILS_CONFIGURE_DIR)) \
		--utility false \
		--output $@

$(COREUTILS_PWD_REPORT): $(COREUTILS_UTILITY_TOOL) $(COREUTILS_RUNTIME_TOOL) \
		$(COREUTILS_CONFIGURE_REPORT) $(LIBC_OUTPUT)
	mkdir -p $(COREUTILS_UTILITY_DIR)
	$(PYTHON) $(COREUTILS_UTILITY_TOOL) \
		--repo-root $(abspath $(REPO_ROOT)) \
		--build $(abspath $(COREUTILS_CONFIGURE_DIR)) \
		--utility pwd \
		--output $@

define COREUTILS_PROBE_RULE
$($(1)_REPORT): $$(COREUTILS_UTILITY_TOOL) $$(COREUTILS_RUNTIME_TOOL) \
		$$(COREUTILS_CONFIGURE_REPORT) $$(LIBC_OUTPUT)
	mkdir -p $$(COREUTILS_UTILITY_DIR)
	$$(PYTHON) $$(COREUTILS_UTILITY_TOOL) \
		--repo-root $$(abspath $$(REPO_ROOT)) \
		--build $$(abspath $$(COREUTILS_CONFIGURE_DIR)) \
		--utility $(2) \
		--output $$@
endef

$(eval $(call COREUTILS_PROBE_RULE,COREUTILS_ECHO,echo))
$(eval $(call COREUTILS_PROBE_RULE,COREUTILS_PRINTF,printf))
$(eval $(call COREUTILS_PROBE_RULE,COREUTILS_BASENAME,basename))
$(eval $(call COREUTILS_PROBE_RULE,COREUTILS_DIRNAME,dirname))
$(eval $(call COREUTILS_PROBE_RULE,COREUTILS_CAT,cat))
$(eval $(call COREUTILS_PROBE_RULE,COREUTILS_WC,wc))
$(eval $(call COREUTILS_PROBE_RULE,COREUTILS_LS,ls))

clean:
	rm -f $(WAST_WASM_TARGET) $(ENGINE_OBJS) $(RT_OBJS) \
		$(WASM_LIB_SHARED_O) $(WASM_LIB_WASM_O) \
		$(WAST_TAB_WASM_O) $(WAST_LEX_WASM_O)
