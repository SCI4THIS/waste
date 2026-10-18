# Real CrashRecoveryContext and its dependencies from the complete Support archive.
RECOVERY_DIR := $(REPO_ROOT)/src/aux/clang/test/crash-recovery
RECOVERY_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/crash-recovery
RECOVERY_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
.PHONY: crash-recovery-fixture test-crash-recovery
crash-recovery-fixture: $(RECOVERY_BUILD)/probe.wasm

$(RECOVERY_BUILD)/probe.o: $(RECOVERY_DIR)/probe.cpp $(RECOVERY_DIR)/crash-recovery.mk $(SDK_HEADERS) | program-execution-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

$(RECOVERY_BUILD)/probe.wasm: $(RECOVERY_BUILD)/probe.o $(PROGRAM_EXECUTION_SUPPORT) $(LOCKS_BUILD)/crt.o $(RECOVERY_CLANG_BUILD)/guest/lib/libLLVMDemangle.a $(RECOVERY_CLANG_BUILD)/cxx/lib/libc++.a $(RECOVERY_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(RECOVERY_DIR)/crash-recovery.mk
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main --export=__stack_pointer \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(RECOVERY_BUILD)/probe.o" "$(LOCKS_BUILD)/crt.o" "$(PROGRAM_EXECUTION_SUPPORT)" \
		"$(RECOVERY_CLANG_BUILD)/guest/lib/libLLVMDemangle.a" \
		"$(RECOVERY_CLANG_BUILD)/cxx/lib/libc++.a" "$(RECOVERY_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-crash-recovery: install-libc aux-test-runner crash-recovery-fixture
	bash "$(RECOVERY_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(RECOVERY_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(RECOVERY_BUILD)/probe.d
