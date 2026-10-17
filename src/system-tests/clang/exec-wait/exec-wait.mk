# Compiled public process calls; WAST owns replacement/reaping expectations.
EXEC_WAIT_DIR := $(REPO_ROOT)/src/system-tests/clang/exec-wait
EXEC_WAIT_BUILD := $(abspath $(BUILD_DIR))/clang/exec-wait
EXEC_WAIT_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang

.PHONY: exec-wait-fixture exec-wait-build-inputs test-exec-wait
exec-wait-fixture: $(EXEC_WAIT_BUILD)/probe.wasm
exec-wait-build-inputs:
	$(MAKE) -C ../aux "$(EXEC_WAIT_CLANG_BUILD)/guest/.configured"

$(EXEC_WAIT_BUILD)/probe.o: $(EXEC_WAIT_DIR)/probe.cpp $(EXEC_WAIT_DIR)/exec-wait.mk $(SDK_HEADERS) | exec-wait-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

$(EXEC_WAIT_BUILD)/probe.wasm: $(EXEC_WAIT_BUILD)/probe.o $(LOCKS_BUILD)/crt.o $(EXEC_WAIT_CLANG_BUILD)/cxx/lib/libc++.a $(EXEC_WAIT_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(EXEC_WAIT_DIR)/exec-wait.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(EXEC_WAIT_BUILD)/probe.o" "$(LOCKS_BUILD)/crt.o" \
		"$(EXEC_WAIT_CLANG_BUILD)/cxx/lib/libc++.a" "$(EXEC_WAIT_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-exec-wait: install-libc aux-test-runner exec-wait-fixture
	bash "$(EXEC_WAIT_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(EXEC_WAIT_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(EXEC_WAIT_BUILD)/probe.d
