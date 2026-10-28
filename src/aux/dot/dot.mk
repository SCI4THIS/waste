# Graphviz dot with statically registered C layout/render plugins and shared libc.
DOT_DIR := $(AUX_DIR)/dot
DOT_BUILD := $(BUILD_DIR)/dot
DOT_SOURCE := $(DOT_BUILD)/source
DOT_UPSTREAM := $(REPO_ROOT)/submodules/graphviz
include $(DOT_DIR)/sources.mk
DOT_SOURCES += lib/cgraph/grammar.c lib/cgraph/scan.c lib/common/htmlparse.c
DOT_INPUTS := $(shell find "$(DOT_UPSTREAM)/lib" "$(DOT_UPSTREAM)/cmd/dot" "$(DOT_UPSTREAM)/plugin/core" "$(DOT_UPSTREAM)/plugin/dot_layout" -type f) $(DOT_UPSTREAM)/CHANGELOG.md $(DOT_UPSTREAM)/graphviz_version.h.in $(DOT_UPSTREAM)/COPYING $(DOT_UPSTREAM)/LICENSE $(DOT_UPSTREAM)/EPL-2.0.txt
DOT_REVISION := $(shell cat "$(DOT_UPSTREAM)/.waste-source-revision" 2>/dev/null || git -C "$(DOT_UPSTREAM)" rev-parse HEAD)
DOT_STAGE := $(DOT_SOURCE)/.prepared-$(DOT_REVISION)
DOT_OBJECTS := $(addprefix $(DOT_BUILD)/objects/,$(DOT_SOURCES:.c=.o))
DOT_STAGED_SOURCES := $(addprefix $(DOT_SOURCE)/,$(DOT_SOURCES))
DOT_INCLUDES := -I"$(DOT_DIR)" -I"$(DOT_SOURCE)" -I"$(DOT_SOURCE)/lib" $(foreach dir,cdt cgraph common gvc pathplan pack,-I"$(DOT_SOURCE)/lib/$(dir)")
DOT_CFLAGS := $(WASM_CFLAGS) -std=gnu17 -DWASTE_SINGLE_THREAD $(DOT_INCLUDES)
DOT_LDFLAGS := --no-entry --import-memory --import-table --export=_start --export=main \
	--export=__heap_base --export=__data_end --export=__stack_pointer \
	--experimental-pic --unresolved-symbols=import-dynamic --gc-sections \
	--global-base=2097152 --table-base=1024 -z stack-size=1048576

.PHONY: dot install-dot test-dot
all: dot
dot: $(DOT_BUILD)/dot.wasm

$(DOT_STAGE): $(DOT_INPUTS) $(DOT_DIR)/stage.py $(DOT_DIR)/waste.patch
	$(PYTHON) "$(DOT_DIR)/stage.py" "$(abspath $(DOT_BUILD))"
	touch "$@"

$(DOT_STAGED_SOURCES): | $(DOT_STAGE)
	@test -f "$@"

$(DOT_OBJECTS): $(DOT_BUILD)/objects/%.o: $(DOT_STAGE) $(DOT_DIR)/config.h $(DOT_DIR)/dot.mk $(DOT_DIR)/sources.mk $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DOT_CFLAGS) -MMD -MP -c "$(DOT_SOURCE)/$*.c" -o "$@"

$(DOT_BUILD)/builtins.o: $(DOT_DIR)/builtins.c $(DOT_STAGE) $(DOT_DIR)/dot.mk $(DOT_DIR)/config.h $(SDK_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DOT_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(DOT_BUILD)/waste-crt.o: $(CRT_SOURCE) $(DOT_DIR)/dot.mk $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) -DWASTE_MAIN_TWO_ARGS -c "$<" -o "$@"

$(DOT_BUILD)/dot.raw.wasm: $(DOT_OBJECTS) $(DOT_BUILD)/builtins.o $(DOT_BUILD)/waste-crt.o $(DOT_DIR)/dot.mk
	$(WASM_LD) $(LDFLAGS) $(DOT_LDFLAGS) -o "$@" $(DOT_OBJECTS) "$(DOT_BUILD)/builtins.o" "$(DOT_BUILD)/waste-crt.o"

$(DOT_BUILD)/dot.wasm: $(DOT_BUILD)/dot.raw.wasm $(LIBC_REVIEW) $(LIBC_OUTPUT)
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(LIBC_OUTPUT)" --rewrite "$<" --output "$@"

install-dot: dot
	$(MAKE) install-libc
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$(DOT_BUILD)/dot.wasm"
	flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(VFS_TOOL)" install --root "$(VFS_ROOT)" --component dot --source "$(DOT_BUILD)/dot.wasm"
	flock "$(BUILD_DIR)/.vfs-install.lock" bash -c 'mkdir -p "$$1/usr/share/licenses/dot"; install -m 644 "$$2/COPYING" "$$2/LICENSE" "$$2/EPL-2.0.txt" "$$1/usr/share/licenses/dot/"' -- "$(VFS_ROOT)" "$(DOT_SOURCE)"

test-dot: install-dot aux-test-runner
	WASTE_TEST_TIMEOUT_MS="$${WASTE_TEST_TIMEOUT_MS:-300000}" bash "$(AUX_DIR)/run-wast-tests.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" dot "$(DOT_DIR)/test/system" "$(abspath $(DOT_BUILD))/tests" "$(abspath $(WASM_RUNNER))" "$(DOT_DIR)/test/system/setup.sh"

-include $(DOT_OBJECTS:.o=.d) $(DOT_BUILD)/builtins.d

.PHONY: dot-source-package
dot-source-package: dot $(DOT_DIR)/source-package.sh
	bash "$(DOT_DIR)/source-package.sh" "$(REPO_ROOT)" "$(abspath $(DOT_BUILD))"
