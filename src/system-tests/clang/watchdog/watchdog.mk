# Whole upstream Watchdog.cpp, shared public alarm and ordinary CRT/libc.
WATCHDOG_DIR := $(REPO_ROOT)/src/system-tests/clang/watchdog
WATCHDOG_BUILD := $(abspath $(BUILD_DIR))/clang/watchdog
WATCHDOG_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang

.PHONY: watchdog-fixture watchdog-build-inputs test-watchdog test-watchdog-controlled
watchdog-fixture: $(WATCHDOG_BUILD)/probe.wasm $(WATCHDOG_BUILD)/controlled.wasm
watchdog-build-inputs:
	$(MAKE) -C ../aux "$(WATCHDOG_CLANG_BUILD)/guest/.configured" CLANG_JOBS=1

$(WATCHDOG_BUILD)/probe.o: $(WATCHDOG_DIR)/probe.cpp $(WATCHDOG_DIR)/watchdog.mk $(SDK_HEADERS) | watchdog-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

$(WATCHDOG_BUILD)/controlled.o: $(WATCHDOG_DIR)/probe.cpp $(WATCHDOG_DIR)/watchdog.mk $(SDK_HEADERS) | watchdog-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -DWASTE_WATCHDOG_CONTROLLED -c "$<" -o "$@"

$(WATCHDOG_BUILD)/methods.o: $(WATCHDOG_CLANG_BUILD)/source/llvm/lib/Support/Watchdog.cpp $(WATCHDOG_CLANG_BUILD)/source/llvm/lib/Support/Unix/Watchdog.inc $(WATCHDOG_DIR)/watchdog.mk $(SDK_HEADERS) | watchdog-build-inputs
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

$(WATCHDOG_BUILD)/%.wasm: $(WATCHDOG_BUILD)/%.o $(WATCHDOG_BUILD)/methods.o $(LOCKS_BUILD)/crt.o $(WATCHDOG_CLANG_BUILD)/cxx/lib/libc++.a $(WATCHDOG_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(WATCHDOG_DIR)/watchdog.mk
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(WATCHDOG_BUILD)/$*.o" "$(WATCHDOG_BUILD)/methods.o" "$(LOCKS_BUILD)/crt.o" \
		"$(WATCHDOG_CLANG_BUILD)/cxx/lib/libc++.a" "$(WATCHDOG_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-watchdog: install-libc aux-test-runner watchdog-fixture
	bash "$(WATCHDOG_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(WATCHDOG_BUILD)" "$(abspath $(WASM_RUNNER))"

test-watchdog-controlled: install-libc watchdog-fixture
	$(MAKE) -C ../cli-rt guest-session guest-session-sanitize
	$(MAKE) -C ../html-rt wast-browser
	cd "$(REPO_ROOT)" && $(PYTHON) tests/guest-session-check.py --scenario clang-watchdog \
		--native "$(REPO_ROOT)/build/cli-rt/private/guest-session-sanitize" \
		--vfs-root "$(VFS_ROOT)" --wasm "$(REPO_ROOT)/build/html-rt/waste-wast.wasm"

-include $(WATCHDOG_BUILD)/probe.d $(WATCHDOG_BUILD)/controlled.d $(WATCHDOG_BUILD)/methods.d
