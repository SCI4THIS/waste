# Compiler-generated registrations and actual LLVM static cache destruction.
SHUTDOWN_DIR := $(REPO_ROOT)/src/system-tests/clang/shutdown
SHUTDOWN_BUILD := $(abspath $(BUILD_DIR))/clang/shutdown
SHUTDOWN_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
.PHONY: shutdown-fixture test-shutdown
shutdown-fixture: $(SHUTDOWN_BUILD)/probe.wasm $(SHUTDOWN_BUILD)/probe-legacy.wasm

$(SHUTDOWN_BUILD)/probe.o: $(SHUTDOWN_DIR)/probe.cpp $(SHUTDOWN_DIR)/shutdown.mk $(SDK_HEADERS) | program-execution-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

$(SHUTDOWN_BUILD)/probe.wasm.raw: $(SHUTDOWN_BUILD)/probe.o $(LOCKS_BUILD)/crt.o $(PROGRAM_EXECUTION_SUPPORT) $(SHUTDOWN_CLANG_BUILD)/guest/lib/libLLVMDemangle.a $(SHUTDOWN_CLANG_BUILD)/cxx/lib/libc++.a $(SHUTDOWN_CLANG_BUILD)/cxx/lib/libc++abi.a $(SHUTDOWN_DIR)/shutdown.mk
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main --export=__stack_pointer \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		--wrap=malloc --wrap=free "$(SHUTDOWN_BUILD)/probe.o" "$(LOCKS_BUILD)/crt.o" \
		"$(PROGRAM_EXECUTION_SUPPORT)" "$(SHUTDOWN_CLANG_BUILD)/guest/lib/libLLVMDemangle.a" \
		"$(SHUTDOWN_CLANG_BUILD)/cxx/lib/libc++.a" "$(SHUTDOWN_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@"

$(SHUTDOWN_BUILD)/probe.wasm: $(SHUTDOWN_BUILD)/probe.wasm.raw $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$<" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

$(SHUTDOWN_BUILD)/probe-legacy.wasm: $(SHUTDOWN_BUILD)/probe.wasm.raw $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(PYTHON) -c 'import sys; sys.path.insert(0, sys.argv[1]); from shared_libc import rewrite; print("Assigned", rewrite(sys.argv[2], sys.argv[3], sys.argv[4], kernel_functions=("exit", "atexit", "__cxa_atexit", "__cxa_finalize")), "imports; retained legacy exit ABI")' "$(REPO_ROOT)/src/aux/libc/tools" "$<" "$(VFS_ROOT)/lib/libc.so.wasm" "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-shutdown: install-libc aux-test-runner shutdown-fixture
	bash "$(SHUTDOWN_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(SHUTDOWN_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(SHUTDOWN_BUILD)/probe.d
