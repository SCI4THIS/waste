# Link the real Support archive; no extracted or substitute LLVM methods.
PROGRAM_EXECUTION_DIR := $(REPO_ROOT)/src/aux/clang/test/program-execution
PROGRAM_EXECUTION_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/program-execution
PROGRAM_EXECUTION_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
PROGRAM_EXECUTION_SUPPORT := $(PROGRAM_EXECUTION_CLANG_BUILD)/guest/lib/libLLVMSupport.a

.PHONY: program-execution-fixture program-execution-build-inputs test-program-execution
program-execution-fixture: $(PROGRAM_EXECUTION_BUILD)/probe.wasm
program-execution-build-inputs:
	$(MAKE) -C ../aux clang-support CLANG_JOBS=1

$(PROGRAM_EXECUTION_SUPPORT): | program-execution-build-inputs
	test -f "$@"

$(PROGRAM_EXECUTION_BUILD)/probe.o: $(PROGRAM_EXECUTION_DIR)/probe.cpp $(PROGRAM_EXECUTION_DIR)/program-execution.mk $(SDK_HEADERS) $(REPO_ROOT)/src/aux/clang/clang-waste.patch $(REPO_ROOT)/src/aux/clang/include/llvm/Support/WasteProgram.h | program-execution-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

$(PROGRAM_EXECUTION_BUILD)/probe.wasm: $(PROGRAM_EXECUTION_BUILD)/probe.o $(PROGRAM_EXECUTION_SUPPORT) $(LOCKS_BUILD)/crt.o $(PROGRAM_EXECUTION_CLANG_BUILD)/guest/lib/libLLVMDemangle.a $(PROGRAM_EXECUTION_CLANG_BUILD)/cxx/lib/libc++.a $(PROGRAM_EXECUTION_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(PROGRAM_EXECUTION_DIR)/program-execution.mk
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(PROGRAM_EXECUTION_BUILD)/probe.o" "$(LOCKS_BUILD)/crt.o" "$(PROGRAM_EXECUTION_SUPPORT)" \
		"$(PROGRAM_EXECUTION_CLANG_BUILD)/guest/lib/libLLVMDemangle.a" \
		"$(PROGRAM_EXECUTION_CLANG_BUILD)/cxx/lib/libc++.a" "$(PROGRAM_EXECUTION_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-program-execution: install-libc aux-test-runner program-execution-fixture
	bash "$(PROGRAM_EXECUTION_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(PROGRAM_EXECUTION_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(PROGRAM_EXECUTION_BUILD)/probe.d
