# Unchanged LLVM timed lock retries and the shared libc sleep deadline.
TIMED_LOCKS_DIR := $(REPO_ROOT)/src/system-tests/clang/timed-locks
TIMED_LOCKS_BUILD := $(abspath $(BUILD_DIR))/clang/timed-locks
TIMED_LOCKS_SESSION_RUNNER ?= $(REPO_ROOT)/build/cli-rt/private/guest-session
.PHONY: timed-locks-fixture test-timed-locks test-usleep-controlled
timed-locks-fixture: $(TIMED_LOCKS_BUILD)/probe.wasm $(TIMED_LOCKS_BUILD)/controlled.wasm

$(TIMED_LOCKS_BUILD)/probe.o: $(TIMED_LOCKS_DIR)/probe.cpp $(TIMED_LOCKS_DIR)/timed-locks.mk $(SDK_HEADERS) | locks-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

$(TIMED_LOCKS_BUILD)/controlled.o: $(TIMED_LOCKS_DIR)/probe.cpp $(TIMED_LOCKS_DIR)/timed-locks.mk $(SDK_HEADERS) | locks-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -DUSLEEP_CONTROLLED -c "$<" -o "$@"

$(TIMED_LOCKS_BUILD)/%.wasm: $(TIMED_LOCKS_BUILD)/%.o $(LOCKS_BUILD)/methods.o $(PLATFORM_BUILD)/category.o $(LOCKS_BUILD)/crt.o $(LOCKS_CLANG_BUILD)/cxx/lib/libc++.a $(LOCKS_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(TIMED_LOCKS_DIR)/timed-locks.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(TIMED_LOCKS_BUILD)/$*.o" "$(LOCKS_BUILD)/methods.o" "$(PLATFORM_BUILD)/category.o" \
		"$(LOCKS_BUILD)/crt.o" "$(LOCKS_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(LOCKS_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-timed-locks: install-libc aux-test-runner timed-locks-fixture
	bash "$(TIMED_LOCKS_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(TIMED_LOCKS_BUILD)" "$(abspath $(WASM_RUNNER))"

test-usleep-controlled: install-libc timed-locks-fixture
	$(MAKE) -C ../cli-rt guest-session guest-session-sanitize
	$(MAKE) -C ../html-rt wast-browser
	cd "$(REPO_ROOT)" && $(PYTHON) tests/guest-session-check.py --scenario clang-usleep \
		--native "$(abspath $(TIMED_LOCKS_SESSION_RUNNER))" --vfs-root "$(VFS_ROOT)" \
		--wasm "$(REPO_ROOT)/build/html-rt/waste-wast.wasm"

-include $(TIMED_LOCKS_BUILD)/probe.d $(TIMED_LOCKS_BUILD)/controlled.d
