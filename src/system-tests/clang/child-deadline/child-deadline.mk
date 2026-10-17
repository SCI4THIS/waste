# Shared deadline primitive for finite LLVM waits; no substitute timer backend.
CHILD_DEADLINE_DIR := $(REPO_ROOT)/src/system-tests/clang/child-deadline
CHILD_DEADLINE_BUILD := $(abspath $(BUILD_DIR))/clang/child-deadline
.PHONY: child-deadline-fixture test-child-deadline test-child-deadline-controlled
child-deadline-fixture: $(CHILD_DEADLINE_BUILD)/probe.wasm $(CHILD_DEADLINE_BUILD)/controlled.wasm $(CHILD_DEADLINE_BUILD)/declarations.o

$(CHILD_DEADLINE_BUILD)/probe.o: $(CHILD_DEADLINE_DIR)/probe.c $(SDK_HEADERS) $(CHILD_DEADLINE_DIR)/child-deadline.mk
	mkdir -p "$(@D)"
	clang $(LOCKS_CFLAGS) -std=c11 -Wall -Wextra -Werror -MMD -MP -c "$<" -o "$@"

$(CHILD_DEADLINE_BUILD)/controlled.o: $(CHILD_DEADLINE_DIR)/probe.c $(SDK_HEADERS) $(CHILD_DEADLINE_DIR)/child-deadline.mk
	mkdir -p "$(@D)"
	clang $(LOCKS_CFLAGS) -std=c11 -Wall -Wextra -Werror -MMD -MP -DWASTE_CHILD_DEADLINE_CONTROLLED -c "$<" -o "$@"

$(CHILD_DEADLINE_BUILD)/declarations.o: $(CHILD_DEADLINE_DIR)/declarations.cpp $(SDK_HEADERS) $(CHILD_DEADLINE_DIR)/child-deadline.mk
	mkdir -p "$(@D)"
	clang++ $(LOCKS_CFLAGS) -std=c++17 -Wall -Wextra -Werror -c "$<" -o "$@"

$(CHILD_DEADLINE_BUILD)/%.wasm: $(CHILD_DEADLINE_BUILD)/%.o $(LOCKS_BUILD)/crt.o $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(CHILD_DEADLINE_DIR)/child-deadline.mk
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(CHILD_DEADLINE_BUILD)/$*.o" "$(LOCKS_BUILD)/crt.o" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-child-deadline: install-libc aux-test-runner child-deadline-fixture
	bash "$(CHILD_DEADLINE_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(CHILD_DEADLINE_BUILD)" "$(abspath $(WASM_RUNNER))"

test-child-deadline-controlled: install-libc child-deadline-fixture
	$(MAKE) -C ../cli-rt guest-session guest-session-sanitize
	$(MAKE) -C ../html-rt wast-browser
	cd "$(REPO_ROOT)" && $(PYTHON) tests/guest-session-check.py --scenario clang-child-deadline \
		--native "$(REPO_ROOT)/build/cli-rt/private/guest-session-sanitize" \
		--vfs-root "$(VFS_ROOT)" --wasm "$(REPO_ROOT)/build/html-rt/waste-wast.wasm"

-include $(CHILD_DEADLINE_BUILD)/probe.d $(CHILD_DEADLINE_BUILD)/controlled.d
