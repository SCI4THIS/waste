# Compiled public session calls; WAST owns detachment expectations.
SESSION_DIR := $(REPO_ROOT)/src/system-tests/clang/session
SESSION_BUILD := $(abspath $(BUILD_DIR))/clang/session
SESSION_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang

.PHONY: session-fixture session-build-inputs test-session
session-fixture: $(SESSION_BUILD)/probe.wasm
session-build-inputs:
	$(MAKE) -C ../aux "$(SESSION_CLANG_BUILD)/guest/.configured"

$(SESSION_BUILD)/probe.o: $(SESSION_DIR)/probe.cpp $(SESSION_DIR)/session.mk $(SDK_HEADERS) | session-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

$(SESSION_BUILD)/probe.wasm: $(SESSION_BUILD)/probe.o $(LOCKS_BUILD)/crt.o $(SESSION_CLANG_BUILD)/cxx/lib/libc++.a $(SESSION_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(SESSION_DIR)/session.mk
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(SESSION_BUILD)/probe.o" "$(LOCKS_BUILD)/crt.o" \
		"$(SESSION_CLANG_BUILD)/cxx/lib/libc++.a" "$(SESSION_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-session: install-libc aux-test-runner session-fixture
	bash "$(SESSION_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(SESSION_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(SESSION_BUILD)/probe.d
