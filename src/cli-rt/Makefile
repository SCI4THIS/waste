CC ?= cc
.DEFAULT_GOAL := applications-native
CPPFLAGS ?=
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -Wpedantic -Werror

ENGINE_DIR := ../engine
BUILD_DIR ?= ../../build/cli-rt
ENGINE_BUILD_DIR ?= ../../build/engine
WAST_GEN_DIR := $(ENGINE_BUILD_DIR)/gen
WASM_LD ?= $(shell command -v wasm-ld 2>/dev/null || printf '%s' $(abspath $(ENGINE_BUILD_DIR)/toolchain/usr/bin/wasm-ld))
WASM_LD_LIB ?= $(abspath $(ENGINE_BUILD_DIR)/toolchain/usr/lib)

# Engine core sources (platform-independent)
ENGINE_SOURCES := $(ENGINE_DIR)/process_driver.c $(ENGINE_DIR)/guest_posix.c $(ENGINE_DIR)/wasm/load.c \
	$(ENGINE_DIR)/vfs.c \
	$(ENGINE_DIR)/source.c \
	$(ENGINE_DIR)/wasm/opcode.c $(ENGINE_DIR)/wasm/encode.c \
	$(ENGINE_DIR)/wast/stream.c $(ENGINE_DIR)/wast/command.c \
	$(ENGINE_DIR)/store.c $(ENGINE_DIR)/checkpoint.c $(ENGINE_DIR)/process.c $(ENGINE_DIR)/lib/kernel.c \
	$(ENGINE_DIR)/lib/select.c $(ENGINE_DIR)/lib/path.c \
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

ENGINE_HEADERS := $(ENGINE_DIR)/wat/types.h $(ENGINE_DIR)/store.h \
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

ENGINE_HEADERS += native_vfs.h $(ENGINE_DIR)/lib/json.h $(ENGINE_DIR)/process_driver.h $(ENGINE_DIR)/vfs.h $(ENGINE_DIR)/guest_posix.h $(ENGINE_DIR)/include/waste.h \
	$(ENGINE_DIR)/../config.h $(ENGINE_DIR)/lib/include/kernel.h \
	$(ENGINE_DIR)/lib/include/path.h
ENGINE_HEADERS += $(ENGINE_DIR)/wast/setup.h

# System-libc runtime profile: one engine object set per optimization/sanitizer
# ABI, shared by application frontends, session harness and batch companion.
NATIVE_OBJECT_DIR := $(BUILD_DIR)/obj/native
SAN_OBJECT_DIR := $(BUILD_DIR)/obj/sanitize
NATIVE_ENGINE_INPUTS := $(filter $(ENGINE_DIR)/%,$(ENGINE_SOURCES))
NATIVE_ENGINE_OBJECTS := $(patsubst $(ENGINE_DIR)/%.c,$(NATIVE_OBJECT_DIR)/engine/%.o,$(NATIVE_ENGINE_INPUTS)) $(NATIVE_OBJECT_DIR)/engine/dispatch_gen.o
SAN_ENGINE_OBJECTS := $(patsubst $(ENGINE_DIR)/%.c,$(SAN_OBJECT_DIR)/engine/%.o,$(NATIVE_ENGINE_INPUTS)) $(SAN_OBJECT_DIR)/engine/dispatch_gen.o
NATIVE_RUNTIME_OBJECTS := $(NATIVE_OBJECT_DIR)/runtime/native_runtime.o $(NATIVE_OBJECT_DIR)/runtime/native_vfs.o $(NATIVE_OBJECT_DIR)/runtime/native_wast.o $(NATIVE_OBJECT_DIR)/runtime/native_terminal.o
SAN_RUNTIME_OBJECTS := $(SAN_OBJECT_DIR)/runtime/native_runtime.o $(SAN_OBJECT_DIR)/runtime/native_vfs.o $(SAN_OBJECT_DIR)/runtime/native_wast.o $(SAN_OBJECT_DIR)/runtime/native_terminal.o
SAN_CFLAGS := -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
NATIVE_RUNTIME_HEADERS := native_runtime.h native_input.h application.h guest_suite.h native_wast.h native_terminal.h

$(NATIVE_OBJECT_DIR)/engine/%.o: $(ENGINE_DIR)/%.c $(ENGINE_HEADERS)
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) -c $< -o $@
$(SAN_OBJECT_DIR)/engine/%.o: $(ENGINE_DIR)/%.c $(ENGINE_HEADERS)
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(SAN_CFLAGS) -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) -c $< -o $@
$(NATIVE_OBJECT_DIR)/engine/dispatch_gen.o: $(WAST_GEN_DIR)/dispatch_gen.c $(ENGINE_HEADERS)
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) -c $< -o $@
$(SAN_OBJECT_DIR)/engine/dispatch_gen.o: $(WAST_GEN_DIR)/dispatch_gen.c $(ENGINE_HEADERS)
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(SAN_CFLAGS) -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) -c $< -o $@
$(NATIVE_OBJECT_DIR)/runtime/%.o: %.c $(ENGINE_HEADERS) $(NATIVE_RUNTIME_HEADERS)
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) -c $< -o $@
$(SAN_OBJECT_DIR)/runtime/%.o: %.c $(ENGINE_HEADERS) $(NATIVE_RUNTIME_HEADERS)
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(SAN_CFLAGS) -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) -c $< -o $@

# Generated parser objects
WAST_TAB_C := $(WAST_GEN_DIR)/wast.tab.c
WAST_TAB_H := $(WAST_GEN_DIR)/wast.tab.h
WAST_LEX_C := $(WAST_GEN_DIR)/wast.lex.c
WAST_TAB_SAN_O := $(BUILD_DIR)/wast.tab.sanitize.o
WAST_LEX_SAN_O := $(BUILD_DIR)/wast.lex.sanitize.o

WAST_GENERATED_CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wno-unused-but-set-variable
WAST_LEX_CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare

# Targets
WAST_TARGET := $(BUILD_DIR)/wast
SUITE_TARGET := $(BUILD_DIR)/private/test-suite
SUITE_SAN_TARGET := $(BUILD_DIR)/private/test-suite-sanitize
SESSION_TARGET := $(BUILD_DIR)/private/guest-session
SESSION_SAN_TARGET := $(BUILD_DIR)/private/guest-session-sanitize
I32_SMOKE_TARGET := $(BUILD_DIR)/i32-smoke-sanitize
WASM_BINARY_TEST_TARGET := $(BUILD_DIR)/wasm-binary-primitives-sanitize
WASM_IMPORT_TEST_TARGET := $(BUILD_DIR)/wasm-import-decode-sanitize
WASM_VALIDATION_TEST_TARGET := $(BUILD_DIR)/wasm-validation-sanitize
PARSER_REENTRANT_TEST_TARGET := $(BUILD_DIR)/wast-parser-reentrant-sanitize
SOURCE_LOADER_TEST_TARGET := $(BUILD_DIR)/source-loader-sanitize
POSIX_SELECT_ABI_TEST_TARGET := $(BUILD_DIR)/posix-select-abi-sanitize
POSIX_PATH_ABI_TEST_TARGET := $(BUILD_DIR)/posix-path-abi-sanitize
POSIX_PATH_VFS_TEST_TARGET := $(BUILD_DIR)/posix-path-vfs-sanitize
POSIX_KERNEL_TEST_TARGET := $(BUILD_DIR)/posix-kernel-sanitize
POSIX_SELECT_TEST_TARGET := $(BUILD_DIR)/posix-select-sanitize
POSIX_WAIT_TEST_TARGET := $(BUILD_DIR)/posix-wait-sanitize
POSIX_SIGNAL_TEST_TARGET := $(BUILD_DIR)/posix-signal-sanitize
CALLER_INSTANCE_TEST_TARGET := $(BUILD_DIR)/caller-instance-sanitize
PROCESS_CONTINUATION_TEST_TARGET := $(BUILD_DIR)/process-continuation-sanitize
TWO_WAIT_TEST_TARGET := $(BUILD_DIR)/two-wait-sanitize
EXEC_MATRIX_TEST_TARGET := $(BUILD_DIR)/exec-matrix-sanitize
CONTINUATION_TEST_TARGET := $(BUILD_DIR)/continuation-sanitize
STORE_CHECKPOINT_TEST_TARGET := $(BUILD_DIR)/store-checkpoint-sanitize
PROCESS_LIFECYCLE_TEST_TARGET := $(BUILD_DIR)/process-lifecycle-sanitize
EXEC_LIFECYCLE_TEST_TARGET := $(BUILD_DIR)/exec-lifecycle-sanitize
I32_SMOKE_WASM := $(BUILD_DIR)/i32-smoke.wasm
IMPORT_SMOKE_WASM := $(BUILD_DIR)/import-smoke.wasm
EXTERN_PROVIDER_WASM := $(BUILD_DIR)/extern-provider.wasm
EXTERN_CONSUMER_WASM := $(BUILD_DIR)/extern-consumer.wasm
CALLER_A_WASM := $(BUILD_DIR)/caller-a.wasm
CALLER_B_WASM := $(BUILD_DIR)/caller-b.wasm
PROCESS_CONTINUATION_WASM := $(BUILD_DIR)/process-continuation.wasm
TWO_WAIT_WASM := $(BUILD_DIR)/two-wait.wasm
EXEC_MATRIX_WASM := $(BUILD_DIR)/exec-matrix.wasm
EXEC_LIFECYCLE_WASM := $(BUILD_DIR)/exec-lifecycle.wasm
CONTINUATION_WASM := $(BUILD_DIR)/continuation.wasm
CONTINUATION_CONSUMER_WASM := $(BUILD_DIR)/continuation-consumer.wasm
WASTE_PROBE_WASM := $(BUILD_DIR)/waste-probe.wasm

.PHONY: wast-native guest-session guest-session-sanitize i32-smoke wasm-binary-primitives wasm-import-decode \
	wasm-validation parser-reentrant source-loader posix-select-abi posix-path-abi posix-path-vfs posix-kernel waste-probe \
	posix-select posix-wait posix-signal caller-instance process-continuation two-wait exec-lifecycle exec-matrix continuation store-checkpoint process-lifecycle gen lint clean

# ---- Parser generation (delegate to engine) ----

gen:
	$(MAKE) -C $(ENGINE_DIR) BUILD_DIR=$(abspath $(ENGINE_BUILD_DIR)) gen

# Materialize shared generated inputs before compiling, including when both
# build directories are new. The recursive engine goal owns their contents;
# an order-only dependency keeps its phony check from recompiling consumers.
$(WAST_TAB_C) $(WAST_TAB_H) $(WAST_LEX_C) $(WAST_GEN_DIR)/dispatch_gen.c $(WAST_GEN_DIR)/dispatch_gen.h: | gen
	@test -f $@

# ---- Dead-code lint (reports unused non-static functions) ----

lint: gen $(BUILD_DIR)/wast.tab.session.o $(BUILD_DIR)/wast.lex.session.o
	@echo "=== Dead-code report (unused non-static engine functions) ==="
	@$(CC) $(CPPFLAGS) $(CFLAGS) \
		-ffunction-sections -fdata-sections \
		-D_POSIX_C_SOURCE=200809L -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) \
		main.c native_vfs.c native_wast.c $(ENGINE_SOURCES) \
		$(BUILD_DIR)/wast.tab.session.o $(BUILD_DIR)/wast.lex.session.o -lm \
		-Wl,--gc-sections -Wl,--print-gc-sections \
		-o /dev/null 2>&1 \
		| grep '\.text\.' \
		| sed 's/.*\.text\.//; s/'"'"' in file.*//' \
		| grep -v -e '^waste_' -e '^wasm_leb_encode' -e '^wasm_writer' \
			-e '^wasm_reader_read_s33' -e '^wasm_reader_remaining' \
			-e '^wasm_decode_module_imports' \
			-e '^wast_stream' -e '^wast_command' -e '^wast_builder' \
			-e '^wast_v128' \
			-e '^native_registered' -e '^native_selected' \
			-e '^abs$$' -e '^copysign$$' -e '^fmax$$' -e '^fmin$$' \
			-e '^frexp' -e '^fwrite$$' -e '^ldexp' -e '^strcpy$$' \
			-e '^strerror$$' \
		| sort -u; \
		echo "---"; \
		echo "(Public API and parser functions are excluded)"

# ---- Compile generated parser objects ----

$(WAST_TAB_SAN_O): $(WAST_TAB_C) $(WAST_TAB_H) $(ENGINE_HEADERS)
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wno-unused-but-set-variable \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-I$(ENGINE_DIR) -I$(WAST_GEN_DIR) -c $(WAST_TAB_C) -o $@

$(WAST_LEX_SAN_O): $(WAST_LEX_C) $(WAST_TAB_H) $(ENGINE_HEADERS)
	mkdir -p $(BUILD_DIR)
	# The generated Flex scanner is large; sanitizer coverage matters here,
	# while optimization makes this test-only translation unit disproportionately
	# expensive to compile.
	$(CC) -O0 -g -std=c11 -Wall -Wextra -Wno-unused-function \
		-Wno-unused-parameter -Wno-sign-compare \
		-D_POSIX_C_SOURCE=200809L \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-I$(ENGINE_DIR) -I$(WAST_GEN_DIR) -c $(WAST_LEX_C) -o $@

# ---- WAST native runner ----

# Compatibility Make alias; the public output is wast.
wast-native: wast

# Batch policy consumes the mounted distribution directly; no browser build
# or Node process is needed. Host child isolation uses system libc, like the
# guest-session companion, while interpreted guest calls keep the guest ABI.
.PHONY: test-suite test-suite-sanitize test-suite-check corpus-native
test-suite: gen $(SUITE_TARGET)

$(SUITE_TARGET): $(NATIVE_OBJECT_DIR)/runtime/test_suite.o $(NATIVE_RUNTIME_OBJECTS) $(NATIVE_OBJECT_DIR)/engine/test_suite.o \
        $(NATIVE_ENGINE_OBJECTS) $(BUILD_DIR)/wast.tab.session.o $(BUILD_DIR)/wast.lex.session.o
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ -lm -o $@

test-suite-sanitize: gen $(SUITE_SAN_TARGET)

test-suite-check: test-suite-sanitize
	python3 ../../tests/native-test-suite-check.py $(SUITE_SAN_TARGET)

$(SUITE_SAN_TARGET): $(SAN_OBJECT_DIR)/runtime/test_suite.o $(SAN_RUNTIME_OBJECTS) $(SAN_OBJECT_DIR)/engine/test_suite.o \
        $(SAN_ENGINE_OBJECTS) $(WAST_TAB_SAN_O) $(WAST_LEX_SAN_O)
	mkdir -p $(@D)
	$(CC) $(SAN_CFLAGS) $^ -lm -o $@

corpus-native: wast
	$(WAST_TARGET) --suite --vfs-root=../vfs \
		--expected-failures=../../tests/native-corpus-expected-failures.txt \
		--jobs=4 --json --results=$(BUILD_DIR)/corpus-results.json

# Compatibility gates now execute authored WAST against the installed DSO in Bash.
.PHONY: libc-native libc-sanitize
libc-native: applications-native
	$(MAKE) -C ../aux test-libc WASM_RUNNER="$(abspath $(BUILD_DIR)/wasm)"
	$(MAKE) -C ../system-tests test-libc WASM_RUNNER="$(abspath $(BUILD_DIR)/wasm)"

libc-sanitize: applications-sanitize
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(MAKE) -C ../aux \
		test-libc WASM_RUNNER="$(abspath $(BUILD_DIR)/wasm-sanitize)"
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(MAKE) -C ../system-tests \
		test-libc WASM_RUNNER="$(abspath $(BUILD_DIR)/wasm-sanitize)"

# ---- i32 smoke test (ASan/UBSan, uses system libc) ----

# The private session harness shares the public system-libc runtime profile.
# Guest filesystem operations remain in the shared engine/kernel.
guest-session: gen $(SESSION_TARGET) test-suite $(BUILD_DIR)/native-corpus-expected-failures.txt

$(SESSION_TARGET): $(NATIVE_OBJECT_DIR)/runtime/guest_session.o $(NATIVE_RUNTIME_OBJECTS) \
        $(NATIVE_ENGINE_OBJECTS) $(BUILD_DIR)/wast.tab.session.o $(BUILD_DIR)/wast.lex.session.o
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ -lm -o $@

$(BUILD_DIR)/wast.tab.session.o: $(WAST_TAB_C) $(WAST_TAB_H) $(ENGINE_HEADERS)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(WAST_GENERATED_CFLAGS) -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) -c $< -o $@

$(BUILD_DIR)/wast.lex.session.o: $(WAST_LEX_C) $(WAST_TAB_H) $(ENGINE_HEADERS)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(WAST_LEX_CFLAGS) -D_POSIX_C_SOURCE=200809L \
		-I$(ENGINE_DIR) -I$(WAST_GEN_DIR) -c $< -o $@

guest-session-sanitize: gen $(SESSION_SAN_TARGET) test-suite-sanitize $(BUILD_DIR)/native-corpus-expected-failures.txt

# Private direct-start ownership gate; guest fixtures live with system tests.
.PHONY: direct-start-build direct-start-check
direct-start-build: gen $(BUILD_DIR)/direct-start-sanitize guest-session
direct-start-check: direct-start-build
	cd ../.. && ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(abspath $(BUILD_DIR))/direct-start-sanitize

$(SAN_OBJECT_DIR)/checks/direct-start.o: ../system-tests/cli-runtime/direct-start.c $(ENGINE_HEADERS)
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(SAN_CFLAGS) -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) -c $< -o $@
$(BUILD_DIR)/direct-start-sanitize: $(SAN_OBJECT_DIR)/checks/direct-start.o $(SAN_ENGINE_OBJECTS) $(WAST_TAB_SAN_O) $(WAST_LEX_SAN_O)
	$(CC) $(SAN_CFLAGS) $^ -lm -o $@

$(SESSION_SAN_TARGET): $(SAN_OBJECT_DIR)/runtime/guest_session.o $(SAN_RUNTIME_OBJECTS) \
        $(SAN_ENGINE_OBJECTS) $(WAST_TAB_SAN_O) $(WAST_LEX_SAN_O)
	mkdir -p $(@D)
	$(CC) $(SAN_CFLAGS) $^ -lm -o $@

.PHONY: wasm wat applications-native applications-sanitize application-check application-check-sanitize
wasm: gen $(BUILD_DIR)/wasm test-suite $(BUILD_DIR)/native-corpus-expected-failures.txt
wat: gen $(BUILD_DIR)/wat test-suite $(BUILD_DIR)/native-corpus-expected-failures.txt
applications-native: wasm wat wast
applications-sanitize: gen $(BUILD_DIR)/wasm-sanitize $(BUILD_DIR)/wat-sanitize $(BUILD_DIR)/wast-sanitize test-suite-sanitize $(BUILD_DIR)/native-corpus-expected-failures.txt
application-check: applications-native
	python3 ../system-tests/cli-runtime/entrypoints/check.py
application-check-sanitize: applications-sanitize
	python3 ../system-tests/cli-runtime/entrypoints/check.py --sanitize

.PHONY: terminal-check terminal-check-sanitize
terminal-check: applications-native
	python3 ../system-tests/cli-runtime/terminal/check.py --idle-seconds=31
terminal-check-sanitize: applications-sanitize
	python3 ../system-tests/cli-runtime/terminal/check.py --sanitize

$(BUILD_DIR)/wasm $(BUILD_DIR)/wat: $(BUILD_DIR)/%: $(NATIVE_OBJECT_DIR)/runtime/%.o \
        $(NATIVE_OBJECT_DIR)/runtime/application.o $(NATIVE_OBJECT_DIR)/runtime/native_input.o \
        $(NATIVE_RUNTIME_OBJECTS) $(NATIVE_ENGINE_OBJECTS) $(BUILD_DIR)/wast.tab.session.o $(BUILD_DIR)/wast.lex.session.o
	$(CC) $(CFLAGS) $^ -lm -o $@
$(BUILD_DIR)/wasm-sanitize $(BUILD_DIR)/wat-sanitize: $(BUILD_DIR)/%-sanitize: $(SAN_OBJECT_DIR)/runtime/%.o \
        $(SAN_OBJECT_DIR)/runtime/application.o $(SAN_OBJECT_DIR)/runtime/native_input.o \
        $(SAN_RUNTIME_OBJECTS) $(SAN_ENGINE_OBJECTS) $(WAST_TAB_SAN_O) $(WAST_LEX_SAN_O)
	$(CC) $(SAN_CFLAGS) $^ -lm -o $@

.PHONY: wast wast-sanitize wast-check wast-check-sanitize
wast: gen $(BUILD_DIR)/wast test-suite $(BUILD_DIR)/native-corpus-expected-failures.txt
wast-sanitize: gen $(BUILD_DIR)/wast-sanitize test-suite-sanitize $(BUILD_DIR)/native-corpus-expected-failures.txt
wast-check: wast
	python3 ../system-tests/cli-runtime/wast/check.py
wast-check-sanitize: wast-sanitize
	python3 ../system-tests/cli-runtime/wast/check.py --sanitize
$(NATIVE_OBJECT_DIR)/runtime/diagnostics.o: main.c $(ENGINE_HEADERS) $(NATIVE_RUNTIME_HEADERS)
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -DWASTE_CLI_NO_MAIN -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) -c $< -o $@
$(SAN_OBJECT_DIR)/runtime/diagnostics.o: main.c $(ENGINE_HEADERS) $(NATIVE_RUNTIME_HEADERS)
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(SAN_CFLAGS) -D_POSIX_C_SOURCE=200809L -DWASTE_CLI_NO_MAIN -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) -c $< -o $@
$(BUILD_DIR)/wast: $(NATIVE_OBJECT_DIR)/runtime/wast.o $(NATIVE_OBJECT_DIR)/runtime/native_input.o \
        $(NATIVE_OBJECT_DIR)/runtime/diagnostics.o $(NATIVE_RUNTIME_OBJECTS) $(NATIVE_ENGINE_OBJECTS) $(BUILD_DIR)/wast.tab.session.o $(BUILD_DIR)/wast.lex.session.o
	$(CC) $(CFLAGS) $^ -lm -o $@
$(BUILD_DIR)/wast-sanitize: $(SAN_OBJECT_DIR)/runtime/wast.o $(SAN_OBJECT_DIR)/runtime/native_input.o \
        $(SAN_OBJECT_DIR)/runtime/diagnostics.o $(SAN_RUNTIME_OBJECTS) $(SAN_ENGINE_OBJECTS) $(WAST_TAB_SAN_O) $(WAST_LEX_SAN_O)
	$(CC) $(SAN_CFLAGS) $^ -lm -o $@

# ---- Minimal external-image ABI probe ----

waste-probe: $(WASTE_PROBE_WASM)

$(WASTE_PROBE_WASM): ../../tests/c-engine-waste-probe.c
	mkdir -p $(BUILD_DIR)
	LD_LIBRARY_PATH=$(WASM_LD_LIB) clang --target=wasm32 -O2 -std=c11 -nostdlib -fno-builtin \
		-fuse-ld=$(WASM_LD) \
		-Wl,--no-entry -Wl,--export-memory \
		-Wl,--allow-undefined -Wl,--export=_start -Wl,--initial-memory=131072 \
		$< -o $@

# ---- i32 smoke test (ASan/UBSan, uses system libc) ----

i32-smoke: gen $(WAST_TARGET) $(SUITE_SAN_TARGET) $(I32_SMOKE_TARGET) $(I32_SMOKE_WASM) $(IMPORT_SMOKE_WASM) $(EXTERN_PROVIDER_WASM) $(EXTERN_CONSUMER_WASM)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(I32_SMOKE_TARGET) $(I32_SMOKE_WASM) $(IMPORT_SMOKE_WASM) $(EXTERN_PROVIDER_WASM) $(EXTERN_CONSUMER_WASM)
	for fixture in ../../tests/engine-regressions/*.wast; do \
		cmp "$$fixture" "../vfs/root/test/engine-regressions/$${fixture##*/}" || exit $$?; \
		$(WAST_TARGET) "$$fixture" || exit $$?; \
	done
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(SUITE_SAN_TARGET) \
		--vfs-root=../vfs --group=engine-regressions --jobs=1 \
		--results=$(BUILD_DIR)/executor-regressions.json

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

$(I32_SMOKE_TARGET): ../../tests/c-engine-i32-smoke.c $(ENGINE_DIR)/wasm/load.c $(ENGINE_DIR)/wasm/opcode.c \
		$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
		$(ENGINE_DIR)/wasm/module.c $(ENGINE_DIR)/wasm/decode.c \
		$(ENGINE_DIR)/op/validate.c \
		$(ENGINE_DIR)/instantiate.c $(ENGINE_DIR)/op/execute.c \
		$(ENGINE_DIR)/op/execute_memory.c \
		$(ENGINE_DIR)/op/execute_numeric.c \
		$(ENGINE_DIR)/op/execute_simd.c \
		$(ENGINE_DIR)/op/execute_gc.c \
		$(ENGINE_DIR)/op/execute_exception.c \
		$(ENGINE_DIR)/op/execute_table.c \
		$(WAST_GEN_DIR)/dispatch_gen.c $(ENGINE_DIR)/instance.c \
		$(ENGINE_DIR)/api.c \
		$(ENGINE_DIR)/engine_internal.h $(ENGINE_DIR)/wasm/opcode.h $(ENGINE_DIR)/wat/types.h \
		$(ENGINE_DIR)/wasm/leb.h $(ENGINE_DIR)/wasm/reader.h \
		$(ENGINE_DIR)/wasm/module.h $(ENGINE_DIR)/wasm/decode.h \
		$(ENGINE_DIR)/op/validate.h \
		$(ENGINE_DIR)/instantiate.h $(ENGINE_DIR)/runtime_internal.h \
		$(ENGINE_DIR)/include/waste.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O0 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-Wno-nonnull-compare \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) \
		../../tests/c-engine-i32-smoke.c $(ENGINE_DIR)/wasm/load.c \
		$(ENGINE_DIR)/wasm/opcode.c $(ENGINE_DIR)/wasm/leb.c \
		$(ENGINE_DIR)/wasm/reader.c $(ENGINE_DIR)/wasm/module.c \
		$(ENGINE_DIR)/wasm/decode.c $(ENGINE_DIR)/op/validate.c \
		$(ENGINE_DIR)/instantiate.c $(ENGINE_DIR)/op/execute.c \
		$(ENGINE_DIR)/op/execute_memory.c \
		$(ENGINE_DIR)/op/execute_numeric.c \
		$(ENGINE_DIR)/op/execute_simd.c \
		$(ENGINE_DIR)/op/execute_gc.c \
		$(ENGINE_DIR)/op/execute_exception.c \
		$(ENGINE_DIR)/op/execute_table.c \
		$(WAST_GEN_DIR)/dispatch_gen.c $(ENGINE_DIR)/instance.c \
		$(ENGINE_DIR)/api.c \
		-lm -o $@

# ---- Shared binary reader/writer tests (ASan/UBSan, uses system libc) ----

wasm-binary-primitives: $(WASM_BINARY_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0 $(WASM_BINARY_TEST_TARGET)

$(WASM_BINARY_TEST_TARGET): ../../tests/wasm-binary-primitives.c \
		$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
		$(ENGINE_DIR)/wasm/writer.c $(ENGINE_DIR)/wasm/leb.h \
		$(ENGINE_DIR)/wasm/reader.h $(ENGINE_DIR)/wasm/writer.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I$(ENGINE_DIR) \
		../../tests/wasm-binary-primitives.c \
		$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
		$(ENGINE_DIR)/wasm/writer.c -o $@

# ---- Decoded import declaration tests (ASan/UBSan, uses system libc) ----

wasm-import-decode: $(WASM_IMPORT_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0 $(WASM_IMPORT_TEST_TARGET)

$(WASM_IMPORT_TEST_TARGET): ../../tests/wasm-import-decode.c \
		$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
		$(ENGINE_DIR)/wasm/writer.c $(ENGINE_DIR)/wasm/module.c \
		$(ENGINE_DIR)/wasm/decode.c $(ENGINE_DIR)/wasm/leb.h \
		$(ENGINE_DIR)/wasm/reader.h $(ENGINE_DIR)/wasm/writer.h \
		$(ENGINE_DIR)/wasm/module.h $(ENGINE_DIR)/wasm/decode.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I$(ENGINE_DIR) \
		../../tests/wasm-import-decode.c \
		$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
		$(ENGINE_DIR)/wasm/writer.c $(ENGINE_DIR)/wasm/module.c \
		$(ENGINE_DIR)/wasm/decode.c -o $@

# ---- Type/control validation tests (ASan/UBSan, uses system libc) ----

wasm-validation: $(WASM_VALIDATION_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0 $(WASM_VALIDATION_TEST_TARGET)

# ---- Concurrent parser isolation test (ASan/UBSan, uses system libc) ----

parser-reentrant: gen $(PARSER_REENTRANT_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0 $(PARSER_REENTRANT_TEST_TARGET)

source-loader: $(SOURCE_LOADER_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0 $(SOURCE_LOADER_TEST_TARGET)

$(SOURCE_LOADER_TEST_TARGET): ../../tests/source-loader.c \
		$(ENGINE_DIR)/source.c $(ENGINE_DIR)/source.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-I$(ENGINE_DIR) ../../tests/source-loader.c \
		$(ENGINE_DIR)/source.c -o $@

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
		$(ENGINE_DIR)/op/validate.c $(ENGINE_DIR)/wasm/opcode.c \
		$(WAST_GEN_DIR)/dispatch_gen.c $(ENGINE_DIR)/op/execute.c \
		$(ENGINE_DIR)/op/execute_memory.c \
		$(ENGINE_DIR)/op/execute_numeric.c \
		$(ENGINE_DIR)/op/execute_simd.c \
		$(ENGINE_DIR)/op/execute_gc.c \
		$(ENGINE_DIR)/op/execute_exception.c \
		$(ENGINE_DIR)/op/execute_table.c \
		$(ENGINE_DIR)/instantiate.c $(ENGINE_DIR)/instance.c \
		$(ENGINE_DIR)/api.c \
		$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
		$(ENGINE_DIR)/wasm/module.c $(ENGINE_DIR)/wasm/decode.c \
		$(ENGINE_DIR)/wasm/load.c \
		$(ENGINE_DIR)/op/validate.h $(WAST_GEN_DIR)/dispatch_gen.h \
		$(ENGINE_DIR)/runtime_internal.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) \
		../../tests/wasm-validation.c \
		$(ENGINE_DIR)/op/validate.c $(ENGINE_DIR)/wasm/opcode.c \
		$(WAST_GEN_DIR)/dispatch_gen.c $(ENGINE_DIR)/op/execute.c \
		$(ENGINE_DIR)/op/execute_memory.c \
		$(ENGINE_DIR)/op/execute_numeric.c \
		$(ENGINE_DIR)/op/execute_simd.c \
		$(ENGINE_DIR)/op/execute_gc.c \
		$(ENGINE_DIR)/op/execute_exception.c \
		$(ENGINE_DIR)/op/execute_table.c \
		$(ENGINE_DIR)/instantiate.c $(ENGINE_DIR)/instance.c \
		$(ENGINE_DIR)/api.c \
		$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
		$(ENGINE_DIR)/wasm/module.c $(ENGINE_DIR)/wasm/decode.c \
		$(ENGINE_DIR)/wasm/load.c \
		-lm -o $@

# ---- POSIX select ABI unit tests (ASan/UBSan, uses system libc) ----

posix-select-abi: $(POSIX_SELECT_ABI_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0 $(POSIX_SELECT_ABI_TEST_TARGET)

posix-path-abi: $(POSIX_PATH_ABI_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0 $(POSIX_PATH_ABI_TEST_TARGET)

posix-path-vfs: gen $(POSIX_PATH_VFS_TEST_TARGET) $(WAST_TARGET) $(SUITE_SAN_TARGET)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(POSIX_PATH_VFS_TEST_TARGET)
	cmp ../../tests/engine-regressions/path-vfs.wast ../vfs/root/test/engine-regressions/path-vfs.wast
	$(WAST_TARGET) ../../tests/engine-regressions/path-vfs.wast
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(SUITE_SAN_TARGET) \
		--vfs-root=../vfs path-vfs.wast --jobs=1 \
		--results=$(BUILD_DIR)/path-regressions.json

.PHONY: vfs-check
vfs-check: $(BUILD_DIR)/vfs-inventory-sanitize $(WAST_TARGET)
	ASAN_OPTIONS=detect_leaks=0 $(BUILD_DIR)/vfs-inventory-sanitize ../vfs
	$(WAST_TARGET) --vfs-root ../vfs ../../tests/vfs-mounted-paths.wast
	$(WAST_TARGET) --vfs-root ../vfs ../../tests/guest-sdk-mounted.wast
	python3 ../../tests/vfs-directory-check.py $(WAST_TARGET)

$(BUILD_DIR)/vfs-inventory-sanitize: native_vfs.c native_vfs.h $(ENGINE_DIR)/lib/json.h ../../tests/c-engine-vfs-inventory.c $(ENGINE_DIR)/vfs.c \
		$(ENGINE_DIR)/vfs.h $(ENGINE_DIR)/lib/kernel.c $(ENGINE_DIR)/lib/path.c \
		$(ENGINE_DIR)/lib/include/kernel.h $(ENGINE_DIR)/lib/include/path.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I. -I$(ENGINE_DIR) \
		../../tests/c-engine-vfs-inventory.c native_vfs.c $(ENGINE_DIR)/vfs.c \
		$(ENGINE_DIR)/lib/kernel.c $(ENGINE_DIR)/lib/select.c $(ENGINE_DIR)/lib/path.c -o $@

$(POSIX_PATH_ABI_TEST_TARGET): ../../tests/posix-path-abi.c \
		$(ENGINE_DIR)/lib/path.c $(ENGINE_DIR)/lib/include/path.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-I$(ENGINE_DIR)/lib \
		../../tests/posix-path-abi.c $(ENGINE_DIR)/lib/path.c -o $@

$(POSIX_PATH_VFS_TEST_TARGET): ../../tests/posix-path-vfs.c \
		$(ENGINE_DIR)/lib/kernel.c $(ENGINE_DIR)/lib/path.c \
		$(ENGINE_DIR)/lib/include/kernel.h $(ENGINE_DIR)/lib/include/path.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-I$(ENGINE_DIR)/lib \
		../../tests/posix-path-vfs.c $(ENGINE_DIR)/lib/kernel.c \
		$(ENGINE_DIR)/lib/select.c $(ENGINE_DIR)/lib/path.c -o $@

$(POSIX_SELECT_ABI_TEST_TARGET): ../../tests/posix-select-abi.c \
		$(ENGINE_DIR)/lib/select.c $(ENGINE_DIR)/lib/include/select.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I$(ENGINE_DIR) \
		../../tests/posix-select-abi.c $(ENGINE_DIR)/lib/select.c \
		-o $@

# ---- POSIX kernel unit tests (ASan/UBSan, uses system libc) ----

posix-kernel: gen $(POSIX_KERNEL_TEST_TARGET) $(WAST_TARGET) $(SUITE_SAN_TARGET) terminal-readiness terminal-timing process-groups terminal-descriptors
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(POSIX_KERNEL_TEST_TARGET)
	cmp ../../tests/engine-regressions/pipe-descriptors.wast ../vfs/root/test/engine-regressions/pipe-descriptors.wast
	$(WAST_TARGET) ../../tests/engine-regressions/pipe-descriptors.wast
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(SUITE_SAN_TARGET) \
		--vfs-root=../vfs pipe-descriptors.wast --jobs=1 \
		--results=$(BUILD_DIR)/pipe-regressions.json
	cmp ../../tests/engine-regressions/descriptor-flags.wast ../vfs/root/test/engine-regressions/descriptor-flags.wast
	$(WAST_TARGET) ../../tests/engine-regressions/descriptor-flags.wast
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(SUITE_SAN_TARGET) \
		--vfs-root=../vfs descriptor-flags.wast --jobs=1 \
		--results=$(BUILD_DIR)/descriptor-regressions.json
	cmp ../../tests/engine-regressions/directory-umask.wast ../vfs/root/test/engine-regressions/directory-umask.wast
	$(WAST_TARGET) ../../tests/engine-regressions/directory-umask.wast
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(SUITE_SAN_TARGET) \
		--vfs-root=../vfs directory-umask.wast --jobs=1 \
		--results=$(BUILD_DIR)/directory-regressions.json
	cmp ../../tests/engine-regressions/shared-memory.wast ../vfs/root/test/engine-regressions/shared-memory.wast
	$(WAST_TARGET) ../../tests/engine-regressions/shared-memory.wast
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(SUITE_SAN_TARGET) \
		--vfs-root=../vfs shared-memory.wast --jobs=1 \
		--results=$(BUILD_DIR)/shared-memory-regressions.json

$(POSIX_KERNEL_TEST_TARGET): ../../tests/posix-kernel.c \
		$(ENGINE_DIR)/lib/kernel.c $(ENGINE_DIR)/lib/select.c \
		$(ENGINE_DIR)/lib/path.c $(ENGINE_DIR)/lib/include/kernel.h \
		$(ENGINE_DIR)/lib/include/select.h $(ENGINE_DIR)/lib/include/path.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I$(ENGINE_DIR) \
		../../tests/posix-kernel.c $(ENGINE_DIR)/lib/kernel.c \
		$(ENGINE_DIR)/lib/select.c $(ENGINE_DIR)/lib/path.c \
		-o $@

# ---- POSIX select/pselect unit tests (ASan/UBSan, uses system libc) ----

posix-select: gen $(POSIX_SELECT_TEST_TARGET) $(WAST_TARGET) $(SUITE_SAN_TARGET) terminal-readiness
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(POSIX_SELECT_TEST_TARGET)
	cmp ../../tests/engine-regressions/select-polling.wast ../vfs/root/test/engine-regressions/select-polling.wast
	$(WAST_TARGET) ../../tests/engine-regressions/select-polling.wast
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(SUITE_SAN_TARGET) \
		--vfs-root=../vfs select-polling.wast --jobs=1 \
		--results=$(BUILD_DIR)/select-regressions.json

$(POSIX_SELECT_TEST_TARGET): ../../tests/posix-select.c \
		$(ENGINE_DIR)/lib/kernel.c $(ENGINE_DIR)/lib/select.c \
		$(ENGINE_DIR)/lib/path.c $(ENGINE_DIR)/lib/include/kernel.h \
		$(ENGINE_DIR)/lib/include/select.h $(ENGINE_DIR)/lib/include/path.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I$(ENGINE_DIR) \
		../../tests/posix-select.c $(ENGINE_DIR)/lib/kernel.c \
		$(ENGINE_DIR)/lib/select.c $(ENGINE_DIR)/lib/path.c \
		-o $@

# ---- POSIX wait-record unit tests (ASan/UBSan, uses system libc) ----

.PHONY: terminal-readiness terminal-timing process-groups terminal-descriptors
.PHONY: diy-posix-control
diy-posix-control: gen $(SESSION_SAN_TARGET)
	cd ../.. && python3 tests/guest-session-diy-control-check.py \
		--native $(abspath $(SESSION_SAN_TARGET)) --vfs-root src/vfs \
		--results $(abspath $(BUILD_DIR)/diy-control-results.json) \
		--asan-options=detect_leaks=0:halt_on_error=1

terminal-readiness: gen $(SESSION_SAN_TARGET)
	cd ../.. && python3 tests/guest-session-readiness-check.py \
		--native $(abspath $(SESSION_SAN_TARGET)) --vfs-root src/vfs \
		--results $(abspath $(BUILD_DIR)/terminal-readiness-results.json) \
		--asan-options=detect_leaks=0:halt_on_error=1

terminal-timing: gen $(SESSION_SAN_TARGET)
	cd ../.. && python3 tests/guest-session-timing-check.py \
		--native $(abspath $(SESSION_SAN_TARGET)) --vfs-root src/vfs \
		--results $(abspath $(BUILD_DIR)/terminal-timing-results.json) \
		--asan-options=detect_leaks=0:halt_on_error=1

process-groups: gen $(SESSION_SAN_TARGET)
	cd ../.. && python3 tests/guest-session-process-group-check.py \
		--native $(abspath $(SESSION_SAN_TARGET)) --vfs-root src/vfs \
		--results $(abspath $(BUILD_DIR)/process-group-results.json) \
		--asan-options=detect_leaks=0:halt_on_error=1

terminal-descriptors: gen $(SESSION_SAN_TARGET)
	cd ../.. && python3 tests/guest-session-terminal-descriptor-check.py \
		--native $(abspath $(SESSION_SAN_TARGET)) --vfs-root src/vfs \
		--results $(abspath $(BUILD_DIR)/terminal-descriptor-results.json) \
		--asan-options=detect_leaks=0:halt_on_error=1

posix-wait: gen $(POSIX_WAIT_TEST_TARGET) $(SESSION_SAN_TARGET)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(POSIX_WAIT_TEST_TARGET)
	cd ../.. && python3 tests/guest-session-wait-check.py \
		--native $(abspath $(SESSION_SAN_TARGET)) --vfs-root src/vfs \
		--asan-options=detect_leaks=0:halt_on_error=1

$(POSIX_WAIT_TEST_TARGET): ../../tests/posix-wait.c \
		$(ENGINE_DIR)/lib/kernel.c $(ENGINE_DIR)/lib/select.c \
		$(ENGINE_DIR)/lib/path.c $(ENGINE_DIR)/lib/include/kernel.h \
		$(ENGINE_DIR)/lib/include/select.h $(ENGINE_DIR)/lib/include/path.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I$(ENGINE_DIR) \
		../../tests/posix-wait.c $(ENGINE_DIR)/lib/kernel.c \
		$(ENGINE_DIR)/lib/select.c $(ENGINE_DIR)/lib/path.c \
		-o $@

# ---- POSIX signal/pselect unit tests (ASan/UBSan, uses system libc) ----

posix-signal: gen $(POSIX_SIGNAL_TEST_TARGET) $(WAST_TARGET) $(SUITE_SAN_TARGET)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(POSIX_SIGNAL_TEST_TARGET)
	cmp ../../tests/engine-regressions/signal-masks.wast ../vfs/root/test/engine-regressions/signal-masks.wast
	$(WAST_TARGET) ../../tests/engine-regressions/signal-masks.wast
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(SUITE_SAN_TARGET) \
		--vfs-root=../vfs signal-masks.wast --jobs=1 \
		--results=$(BUILD_DIR)/signal-regressions.json

$(POSIX_SIGNAL_TEST_TARGET): ../../tests/posix-signal.c \
		$(ENGINE_DIR)/lib/kernel.c $(ENGINE_DIR)/lib/select.c \
		$(ENGINE_DIR)/lib/path.c $(ENGINE_DIR)/lib/include/kernel.h \
		$(ENGINE_DIR)/lib/include/select.h $(ENGINE_DIR)/lib/include/path.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I$(ENGINE_DIR) \
		../../tests/posix-signal.c $(ENGINE_DIR)/lib/kernel.c \
		$(ENGINE_DIR)/lib/select.c $(ENGINE_DIR)/lib/path.c \
		-o $@

# ---- Caller-instance host import tests (ASan/UBSan, uses system libc) ----

caller-instance: gen $(WAST_TARGET) $(SUITE_SAN_TARGET) $(CALLER_INSTANCE_TEST_TARGET) $(CALLER_A_WASM) $(CALLER_B_WASM)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(CALLER_INSTANCE_TEST_TARGET) $(CALLER_A_WASM) $(CALLER_B_WASM)
	cmp ../../tests/engine-regressions/caller-memory.wast ../vfs/root/test/engine-regressions/caller-memory.wast
	$(WAST_TARGET) ../../tests/engine-regressions/caller-memory.wast
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(SUITE_SAN_TARGET) \
		--vfs-root=../vfs caller-memory.wast --results=$(BUILD_DIR)/caller-memory-results.json

$(CALLER_A_WASM): ../../tests/c-engine-caller-a.wat
	mkdir -p $(BUILD_DIR)
	wasm-as --enable-multivalue $< -o $@

$(CALLER_B_WASM): ../../tests/c-engine-caller-b.wat
	mkdir -p $(BUILD_DIR)
	wasm-as --enable-multivalue $< -o $@

$(CALLER_INSTANCE_TEST_TARGET): ../../tests/c-engine-caller-instance.c \
		$(ENGINE_DIR)/wasm/load.c $(ENGINE_DIR)/wasm/opcode.c \
		$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
		$(ENGINE_DIR)/wasm/module.c $(ENGINE_DIR)/wasm/decode.c \
		$(ENGINE_DIR)/op/validate.c \
		$(ENGINE_DIR)/instantiate.c $(ENGINE_DIR)/op/execute.c \
		$(ENGINE_DIR)/op/execute_memory.c \
		$(ENGINE_DIR)/op/execute_numeric.c \
		$(ENGINE_DIR)/op/execute_simd.c \
		$(ENGINE_DIR)/op/execute_gc.c \
		$(ENGINE_DIR)/op/execute_exception.c \
		$(ENGINE_DIR)/op/execute_table.c \
		$(WAST_GEN_DIR)/dispatch_gen.c $(ENGINE_DIR)/instance.c \
		$(ENGINE_DIR)/api.c \
		$(ENGINE_DIR)/engine_internal.h $(ENGINE_DIR)/include/waste.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer -I$(ENGINE_DIR) -I$(WAST_GEN_DIR) \
		../../tests/c-engine-caller-instance.c $(ENGINE_DIR)/wasm/load.c \
		$(ENGINE_DIR)/wasm/opcode.c $(ENGINE_DIR)/wasm/leb.c \
		$(ENGINE_DIR)/wasm/reader.c $(ENGINE_DIR)/wasm/module.c \
		$(ENGINE_DIR)/wasm/decode.c $(ENGINE_DIR)/op/validate.c \
		$(ENGINE_DIR)/instantiate.c $(ENGINE_DIR)/op/execute.c \
		$(ENGINE_DIR)/op/execute_memory.c \
		$(ENGINE_DIR)/op/execute_numeric.c \
		$(ENGINE_DIR)/op/execute_simd.c \
		$(ENGINE_DIR)/op/execute_gc.c \
		$(ENGINE_DIR)/op/execute_exception.c \
		$(ENGINE_DIR)/op/execute_table.c \
		$(WAST_GEN_DIR)/dispatch_gen.c $(ENGINE_DIR)/instance.c \
		$(ENGINE_DIR)/api.c \
		-lm -o $@

process-continuation: $(PROCESS_CONTINUATION_TEST_TARGET) $(PROCESS_CONTINUATION_WASM)
	ASAN_OPTIONS=detect_leaks=0 $(PROCESS_CONTINUATION_TEST_TARGET) $(PROCESS_CONTINUATION_WASM)

two-wait: $(TWO_WAIT_TEST_TARGET) $(TWO_WAIT_WASM)
	ASAN_OPTIONS=detect_leaks=0 $(TWO_WAIT_TEST_TARGET) $(TWO_WAIT_WASM)

exec-matrix: $(EXEC_MATRIX_TEST_TARGET) $(EXEC_MATRIX_WASM)
	ASAN_OPTIONS=detect_leaks=0 $(EXEC_MATRIX_TEST_TARGET) $(abspath $(EXEC_MATRIX_WASM))

exec-lifecycle: $(EXEC_LIFECYCLE_TEST_TARGET) $(EXEC_LIFECYCLE_WASM)
	ASAN_OPTIONS=detect_leaks=0 $(EXEC_LIFECYCLE_TEST_TARGET) $(abspath $(EXEC_LIFECYCLE_WASM))

$(PROCESS_CONTINUATION_WASM): ../../tests/c-engine-process-continuation.wat
	mkdir -p $(BUILD_DIR)
	wasm-as --enable-multivalue $< -o $@

$(TWO_WAIT_WASM): ../../tests/c-engine-two-wait.wat
	mkdir -p $(BUILD_DIR)
	wasm-as --enable-multivalue $< -o $@

$(EXEC_MATRIX_WASM): ../../tests/c-engine-exec-matrix.wat
	mkdir -p $(BUILD_DIR)
	wasm-as --enable-multivalue $< -o $@

$(EXEC_LIFECYCLE_WASM): ../../tests/c-engine-exec-lifecycle.wat
	mkdir -p $(BUILD_DIR)
	wasm-as --enable-multivalue $< -o $@

$(PROCESS_CONTINUATION_TEST_TARGET): ../../tests/c-engine-process-continuation.c \
		$(ENGINE_DIR)/wasm/load.c $(ENGINE_DIR)/wasm/opcode.c \
		$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
		$(ENGINE_DIR)/wasm/module.c $(ENGINE_DIR)/wasm/decode.c \
		$(ENGINE_DIR)/op/validate.c $(ENGINE_DIR)/instantiate.c \
		$(ENGINE_DIR)/op/execute.c $(ENGINE_DIR)/op/execute_memory.c \
		$(ENGINE_DIR)/op/execute_numeric.c $(ENGINE_DIR)/op/execute_simd.c \
		$(ENGINE_DIR)/op/execute_gc.c $(ENGINE_DIR)/op/execute_exception.c \
		$(ENGINE_DIR)/op/execute_table.c $(WAST_GEN_DIR)/dispatch_gen.c \
		$(ENGINE_DIR)/instance.c $(ENGINE_DIR)/api.c \
		$(ENGINE_DIR)/engine_internal.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-I$(ENGINE_DIR) -I$(WAST_GEN_DIR) \
		../../tests/c-engine-process-continuation.c \
		$(ENGINE_DIR)/wasm/load.c $(ENGINE_DIR)/wasm/opcode.c \
		$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
		$(ENGINE_DIR)/wasm/module.c $(ENGINE_DIR)/wasm/decode.c \
		$(ENGINE_DIR)/op/validate.c $(ENGINE_DIR)/instantiate.c \
		$(ENGINE_DIR)/op/execute.c $(ENGINE_DIR)/op/execute_memory.c \
		$(ENGINE_DIR)/op/execute_numeric.c $(ENGINE_DIR)/op/execute_simd.c \
		$(ENGINE_DIR)/op/execute_gc.c $(ENGINE_DIR)/op/execute_exception.c \
		$(ENGINE_DIR)/op/execute_table.c $(WAST_GEN_DIR)/dispatch_gen.c \
		$(ENGINE_DIR)/instance.c $(ENGINE_DIR)/api.c -lm -o $@

$(TWO_WAIT_TEST_TARGET): ../../tests/c-engine-two-wait.c \
		$(ENGINE_DIR)/wasm/load.c $(ENGINE_DIR)/wasm/opcode.c \
		$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
		$(ENGINE_DIR)/wasm/module.c $(ENGINE_DIR)/wasm/decode.c \
		$(ENGINE_DIR)/op/validate.c $(ENGINE_DIR)/instantiate.c \
		$(ENGINE_DIR)/op/execute.c $(ENGINE_DIR)/op/execute_memory.c \
		$(ENGINE_DIR)/op/execute_numeric.c $(ENGINE_DIR)/op/execute_simd.c \
		$(ENGINE_DIR)/op/execute_gc.c $(ENGINE_DIR)/op/execute_exception.c \
		$(ENGINE_DIR)/op/execute_table.c $(WAST_GEN_DIR)/dispatch_gen.c \
		$(ENGINE_DIR)/instance.c $(ENGINE_DIR)/api.c $(ENGINE_DIR)/engine_internal.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-I$(ENGINE_DIR) -I$(WAST_GEN_DIR) \
		../../tests/c-engine-two-wait.c \
		$(ENGINE_DIR)/wasm/load.c $(ENGINE_DIR)/wasm/opcode.c \
		$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
		$(ENGINE_DIR)/wasm/module.c $(ENGINE_DIR)/wasm/decode.c \
		$(ENGINE_DIR)/op/validate.c $(ENGINE_DIR)/instantiate.c \
		$(ENGINE_DIR)/op/execute.c $(ENGINE_DIR)/op/execute_memory.c \
		$(ENGINE_DIR)/op/execute_numeric.c $(ENGINE_DIR)/op/execute_simd.c \
		$(ENGINE_DIR)/op/execute_gc.c $(ENGINE_DIR)/op/execute_exception.c \
		$(ENGINE_DIR)/op/execute_table.c $(WAST_GEN_DIR)/dispatch_gen.c \
		$(ENGINE_DIR)/instance.c $(ENGINE_DIR)/api.c -lm -o $@

$(EXEC_MATRIX_TEST_TARGET): ../../tests/c-engine-exec-matrix.c \
		$(ENGINE_DIR)/store.c $(ENGINE_DIR)/source.c $(ENGINE_DIR)/process.c \
		$(ENGINE_DIR)/lib/kernel.c $(ENGINE_DIR)/lib/path.c $(ENGINE_DIR)/lib/select.c \
		$(ENGINE_DIR)/checkpoint.c $(ENGINE_DIR)/wasm/load.c \
		$(ENGINE_DIR)/wasm/opcode.c $(ENGINE_DIR)/wasm/leb.c \
		$(ENGINE_DIR)/wasm/reader.c $(ENGINE_DIR)/wasm/module.c \
		$(ENGINE_DIR)/wasm/decode.c $(ENGINE_DIR)/op/validate.c \
		$(ENGINE_DIR)/instantiate.c $(ENGINE_DIR)/op/execute.c \
		$(ENGINE_DIR)/op/execute_memory.c $(ENGINE_DIR)/op/execute_numeric.c \
		$(ENGINE_DIR)/op/execute_simd.c $(ENGINE_DIR)/op/execute_gc.c \
		$(ENGINE_DIR)/op/execute_exception.c $(ENGINE_DIR)/op/execute_table.c \
		$(WAST_GEN_DIR)/dispatch_gen.c $(WAST_GEN_DIR)/wast.tab.c \
		$(WAST_GEN_DIR)/wast.lex.c $(ENGINE_DIR)/instance.c $(ENGINE_DIR)/api.c
	mkdir -p $(BUILD_DIR)
	$(CC) -O0 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-Wno-nonnull-compare \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-I$(ENGINE_DIR) -I$(ENGINE_DIR)/lib/include -I$(WAST_GEN_DIR) \
		../../tests/c-engine-exec-matrix.c $(ENGINE_DIR)/store.c \
		$(ENGINE_DIR)/source.c \
		$(ENGINE_DIR)/process.c $(ENGINE_DIR)/lib/kernel.c \
		$(ENGINE_DIR)/lib/path.c $(ENGINE_DIR)/lib/select.c \
		$(ENGINE_DIR)/checkpoint.c $(ENGINE_DIR)/wasm/load.c \
		$(ENGINE_DIR)/wasm/opcode.c $(ENGINE_DIR)/wasm/leb.c \
		$(ENGINE_DIR)/wasm/reader.c $(ENGINE_DIR)/wasm/module.c \
		$(ENGINE_DIR)/wasm/decode.c $(ENGINE_DIR)/op/validate.c \
		$(ENGINE_DIR)/instantiate.c $(ENGINE_DIR)/op/execute.c \
		$(ENGINE_DIR)/op/execute_memory.c $(ENGINE_DIR)/op/execute_numeric.c \
		$(ENGINE_DIR)/op/execute_simd.c $(ENGINE_DIR)/op/execute_gc.c \
		$(ENGINE_DIR)/op/execute_exception.c $(ENGINE_DIR)/op/execute_table.c \
		$(WAST_GEN_DIR)/dispatch_gen.c $(ENGINE_DIR)/instance.c \
		$(ENGINE_DIR)/api.c \
		../aux/libc/runtime/string.c ../aux/libc/runtime/stdlib.c \
		../aux/libc/runtime/math.c ../aux/libc/runtime/errno.c \
		lib/stdio.c -lm -o $@

$(EXEC_LIFECYCLE_TEST_TARGET): ../../tests/c-engine-exec-lifecycle.c \
		$(ENGINE_DIR)/wasm/load.c $(ENGINE_DIR)/wasm/opcode.c \
		$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
		$(ENGINE_DIR)/wasm/module.c $(ENGINE_DIR)/wasm/decode.c \
		$(ENGINE_DIR)/op/validate.c $(ENGINE_DIR)/instantiate.c \
		$(ENGINE_DIR)/op/execute.c $(ENGINE_DIR)/op/execute_memory.c \
		$(ENGINE_DIR)/op/execute_numeric.c $(ENGINE_DIR)/op/execute_simd.c \
		$(ENGINE_DIR)/op/execute_gc.c $(ENGINE_DIR)/op/execute_exception.c \
		$(ENGINE_DIR)/op/execute_table.c $(WAST_GEN_DIR)/dispatch_gen.c \
		$(ENGINE_DIR)/instance.c $(ENGINE_DIR)/api.c \
		$(ENGINE_DIR)/engine_internal.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-I$(ENGINE_DIR) -I$(WAST_GEN_DIR) \
		../../tests/c-engine-exec-lifecycle.c \
		$(ENGINE_DIR)/wasm/load.c $(ENGINE_DIR)/wasm/opcode.c \
		$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
		$(ENGINE_DIR)/wasm/module.c $(ENGINE_DIR)/wasm/decode.c \
		$(ENGINE_DIR)/op/validate.c $(ENGINE_DIR)/instantiate.c \
		$(ENGINE_DIR)/op/execute.c $(ENGINE_DIR)/op/execute_memory.c \
		$(ENGINE_DIR)/op/execute_numeric.c $(ENGINE_DIR)/op/execute_simd.c \
		$(ENGINE_DIR)/op/execute_gc.c $(ENGINE_DIR)/op/execute_exception.c \
		$(ENGINE_DIR)/op/execute_table.c $(WAST_GEN_DIR)/dispatch_gen.c \
		$(ENGINE_DIR)/instance.c $(ENGINE_DIR)/api.c -lm -o $@

continuation: gen $(WAST_TARGET) $(SUITE_SAN_TARGET) $(CONTINUATION_TEST_TARGET) $(CONTINUATION_WASM) $(CONTINUATION_CONSUMER_WASM)
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(CONTINUATION_TEST_TARGET) $(CONTINUATION_WASM) \
		$(CONTINUATION_CONSUMER_WASM)
	cmp ../../tests/engine-regressions/continuation-waits.wast ../vfs/root/test/engine-regressions/continuation-waits.wast
	$(WAST_TARGET) ../../tests/engine-regressions/continuation-waits.wast
	cmp ../../tests/engine-regressions/setjmp-buffer-copy.wast ../vfs/root/test/engine-regressions/setjmp-buffer-copy.wast
	$(WAST_TARGET) ../../tests/engine-regressions/setjmp-buffer-copy.wast
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(SUITE_SAN_TARGET) \
		--vfs-root=../vfs setjmp-buffer-copy.wast --results=$(BUILD_DIR)/setjmp-buffer-copy-results.json
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(SUITE_SAN_TARGET) \
		--vfs-root=../vfs continuation-waits.wast --results=$(BUILD_DIR)/continuation-waits-results.json

store-checkpoint: $(STORE_CHECKPOINT_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0 $(STORE_CHECKPOINT_TEST_TARGET)

process-lifecycle: $(PROCESS_LIFECYCLE_TEST_TARGET)
	ASAN_OPTIONS=detect_leaks=0 $(PROCESS_LIFECYCLE_TEST_TARGET)

$(PROCESS_LIFECYCLE_TEST_TARGET): ../../tests/c-engine-process-lifecycle.c \
		$(ENGINE_SOURCES) $(ENGINE_HEADERS) $(WAST_TAB_SAN_O) $(WAST_LEX_SAN_O)
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-I$(ENGINE_DIR) -I$(ENGINE_DIR)/include -I$(WAST_GEN_DIR) \
		../../tests/c-engine-process-lifecycle.c $(ENGINE_SOURCES) \
		$(WAST_TAB_SAN_O) $(WAST_LEX_SAN_O) -lm -o $@

$(STORE_CHECKPOINT_TEST_TARGET): ../../tests/c-engine-store-checkpoint.c \
		$(ENGINE_DIR)/checkpoint.c $(ENGINE_DIR)/lib/kernel.c \
		$(ENGINE_DIR)/lib/select.c \
		$(ENGINE_DIR)/wasm/load.c \
		$(ENGINE_DIR)/wasm/opcode.c $(ENGINE_DIR)/wasm/leb.c \
		$(ENGINE_DIR)/wasm/reader.c $(ENGINE_DIR)/wasm/module.c \
		$(ENGINE_DIR)/wasm/decode.c $(ENGINE_DIR)/op/validate.c \
		$(ENGINE_DIR)/instantiate.c $(ENGINE_DIR)/op/execute.c \
		$(ENGINE_DIR)/op/execute_memory.c \
		$(ENGINE_DIR)/op/execute_numeric.c \
		$(ENGINE_DIR)/op/execute_simd.c $(ENGINE_DIR)/op/execute_gc.c \
		$(ENGINE_DIR)/op/execute_exception.c \
		$(ENGINE_DIR)/op/execute_table.c $(WAST_GEN_DIR)/dispatch_gen.c \
		$(ENGINE_DIR)/instance.c $(ENGINE_DIR)/api.c $(ENGINE_DIR)/store.h \
		$(ENGINE_DIR)/runtime_internal.h $(ENGINE_DIR)/engine_internal.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-I$(ENGINE_DIR) -I$(ENGINE_DIR)/include -I$(WAST_GEN_DIR) \
		../../tests/c-engine-store-checkpoint.c $(ENGINE_DIR)/checkpoint.c \
		$(ENGINE_DIR)/lib/kernel.c $(ENGINE_DIR)/lib/path.c \
		$(ENGINE_DIR)/lib/select.c \
		$(ENGINE_DIR)/wasm/load.c $(ENGINE_DIR)/wasm/opcode.c \
		$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
		$(ENGINE_DIR)/wasm/module.c $(ENGINE_DIR)/wasm/decode.c \
		$(ENGINE_DIR)/op/validate.c $(ENGINE_DIR)/instantiate.c \
		$(ENGINE_DIR)/op/execute.c $(ENGINE_DIR)/op/execute_memory.c \
		$(ENGINE_DIR)/op/execute_numeric.c $(ENGINE_DIR)/op/execute_simd.c \
		$(ENGINE_DIR)/op/execute_gc.c $(ENGINE_DIR)/op/execute_exception.c \
		$(ENGINE_DIR)/op/execute_table.c $(WAST_GEN_DIR)/dispatch_gen.c \
		$(ENGINE_DIR)/instance.c $(ENGINE_DIR)/api.c -lm -o $@

$(CONTINUATION_WASM): ../../tests/c-engine-continuation.wat
	mkdir -p $(BUILD_DIR)
	wasm-as --enable-multivalue $< -o $@

$(CONTINUATION_CONSUMER_WASM): ../../tests/c-engine-continuation-consumer.wat
	mkdir -p $(BUILD_DIR)
	wasm-as --enable-multivalue $< -o $@

$(CONTINUATION_TEST_TARGET): ../../tests/c-engine-continuation.c \
		$(ENGINE_DIR)/wasm/load.c $(ENGINE_DIR)/wasm/opcode.c \
		$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
		$(ENGINE_DIR)/wasm/module.c $(ENGINE_DIR)/wasm/decode.c \
		$(ENGINE_DIR)/op/validate.c $(ENGINE_DIR)/instantiate.c \
		$(ENGINE_DIR)/op/execute.c $(ENGINE_DIR)/op/execute_memory.c \
		$(ENGINE_DIR)/op/execute_numeric.c $(ENGINE_DIR)/op/execute_simd.c \
		$(ENGINE_DIR)/op/execute_gc.c $(ENGINE_DIR)/op/execute_exception.c \
		$(ENGINE_DIR)/op/execute_table.c $(WAST_GEN_DIR)/dispatch_gen.c \
		$(ENGINE_DIR)/instance.c $(ENGINE_DIR)/api.c \
		$(ENGINE_DIR)/engine_internal.h $(ENGINE_DIR)/runtime_internal.h
	mkdir -p $(BUILD_DIR)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-I$(ENGINE_DIR) -I$(WAST_GEN_DIR) \
		../../tests/c-engine-continuation.c \
		$(ENGINE_DIR)/wasm/load.c $(ENGINE_DIR)/wasm/opcode.c \
		$(ENGINE_DIR)/wasm/leb.c $(ENGINE_DIR)/wasm/reader.c \
		$(ENGINE_DIR)/wasm/module.c $(ENGINE_DIR)/wasm/decode.c \
		$(ENGINE_DIR)/op/validate.c $(ENGINE_DIR)/instantiate.c \
		$(ENGINE_DIR)/op/execute.c $(ENGINE_DIR)/op/execute_memory.c \
		$(ENGINE_DIR)/op/execute_numeric.c $(ENGINE_DIR)/op/execute_simd.c \
		$(ENGINE_DIR)/op/execute_gc.c $(ENGINE_DIR)/op/execute_exception.c \
		$(ENGINE_DIR)/op/execute_table.c $(WAST_GEN_DIR)/dispatch_gen.c \
		$(ENGINE_DIR)/instance.c $(ENGINE_DIR)/api.c -lm -o $@

clean:
	rm -f $(BUILD_DIR)/wasm $(BUILD_DIR)/wat $(WAST_TARGET) \
		$(BUILD_DIR)/wasm-sanitize $(BUILD_DIR)/wat-sanitize $(BUILD_DIR)/wast-sanitize \
		$(SUITE_TARGET) $(SUITE_SAN_TARGET) $(SESSION_TARGET) $(SESSION_SAN_TARGET) \
		$(I32_SMOKE_TARGET) $(WASM_BINARY_TEST_TARGET) \
		$(WASM_IMPORT_TEST_TARGET) $(WASM_VALIDATION_TEST_TARGET) \
		$(PARSER_REENTRANT_TEST_TARGET) $(POSIX_SELECT_ABI_TEST_TARGET) \
		$(SOURCE_LOADER_TEST_TARGET) \
		$(POSIX_KERNEL_TEST_TARGET) $(CALLER_INSTANCE_TEST_TARGET) \
		$(PROCESS_CONTINUATION_TEST_TARGET) $(PROCESS_CONTINUATION_WASM) \
		$(CONTINUATION_TEST_TARGET) $(CONTINUATION_WASM) \
		$(CONTINUATION_CONSUMER_WASM) \
		$(BUILD_DIR)/wast.tab.session.o $(BUILD_DIR)/wast.lex.session.o $(WAST_TAB_SAN_O) $(WAST_LEX_SAN_O)

.PHONY: guest-test-launcher
guest-test-launcher: $(BUILD_DIR)/waste-test.wasm

GUEST_TEST_MAX_MEMORY = $(shell python3 ../html-rt/tools/runtime_config.py --get GUEST_TEST_MAX_MEMORY_BYTES)

$(BUILD_DIR)/waste-test.wasm: ../html-rt/commands/waste-test.c ../config.h ../html-rt/tools/runtime_config.py
	mkdir -p $(BUILD_DIR)
	LD_LIBRARY_PATH=$(WASM_LD_LIB) clang --target=wasm32 -O2 -std=c11 -Wall -Wextra -Werror -nostdlib -fno-builtin \
		-fuse-ld=$(WASM_LD) -Wl,--no-entry -Wl,--export-memory -Wl,--allow-undefined \
		-Wl,--export=_start -Wl,--max-memory=$(GUEST_TEST_MAX_MEMORY) $< -o $@

$(BUILD_DIR)/native-corpus-expected-failures.txt: ../../tests/native-corpus-expected-failures.txt
	mkdir -p $(BUILD_DIR)
	cp $< $@

.PHONY: guest-test-install guest-test-check
guest-test-install: guest-test-launcher
	python3 ../html-rt/tools/vfs.py install --component waste-test --source $(BUILD_DIR)/waste-test.wasm

guest-test-check: guest-session
	cd ../.. && python3 tests/guest-test-launcher-check.py --native $(abspath $(SESSION_TARGET)) \
		--vfs-root $(abspath ../vfs)

# Shared configuration also reaches the small standalone C gates.
$(WASM_BINARY_TEST_TARGET) \
	$(WASM_IMPORT_TEST_TARGET) \
	$(SOURCE_LOADER_TEST_TARGET) \
	$(PARSER_REENTRANT_TEST_TARGET) \
	$(WASM_VALIDATION_TEST_TARGET) \
	$(POSIX_PATH_ABI_TEST_TARGET) \
	$(POSIX_PATH_VFS_TEST_TARGET) \
	$(POSIX_SELECT_ABI_TEST_TARGET) \
	$(POSIX_KERNEL_TEST_TARGET) \
	$(POSIX_SELECT_TEST_TARGET) \
	$(POSIX_WAIT_TEST_TARGET) \
	$(POSIX_SIGNAL_TEST_TARGET) \
	$(CALLER_INSTANCE_TEST_TARGET) \
	$(PROCESS_CONTINUATION_TEST_TARGET) \
	$(TWO_WAIT_TEST_TARGET) \
	$(EXEC_MATRIX_TEST_TARGET) \
	$(EXEC_LIFECYCLE_TEST_TARGET) \
	$(PROCESS_LIFECYCLE_TEST_TARGET) \
	$(STORE_CHECKPOINT_TEST_TARGET) \
	$(CONTINUATION_TEST_TARGET): ../config.h
