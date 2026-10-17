# Actual staged LLVM Wait; the whole Program translation unit remains a gate.
PROGRAM_WAIT_DIR := $(REPO_ROOT)/src/system-tests/clang/program-wait
PROGRAM_WAIT_BUILD := $(abspath $(BUILD_DIR))/clang/program-wait
PROGRAM_WAIT_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
PROGRAM_WAIT_HEADERS := $(REPO_ROOT)/src/aux/clang/include/llvm/Support/WasteProgram.h

.PHONY: program-wait-fixture program-wait-build-inputs test-program-wait
program-wait-fixture: $(PROGRAM_WAIT_BUILD)/probe.wasm
program-wait-build-inputs:
	$(MAKE) -C ../aux "$(PROGRAM_WAIT_CLANG_BUILD)/guest/.configured"

$(PROGRAM_WAIT_BUILD)/probe.o: $(PROGRAM_WAIT_DIR)/probe.cpp $(PROGRAM_WAIT_DIR)/program-wait.mk $(SDK_HEADERS) | program-wait-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

$(PROGRAM_WAIT_BUILD)/methods.cpp: $(PROGRAM_WAIT_DIR)/prepare-wait.py $(PROGRAM_WAIT_CLANG_BUILD)/source/llvm/lib/Support/Unix/Program.inc $(PROGRAM_WAIT_CLANG_BUILD)/source/llvm/lib/Support/Unix/Unix.h | program-wait-build-inputs
	$(PYTHON) "$<" "$(PROGRAM_WAIT_CLANG_BUILD)/source" "$@"

$(PROGRAM_WAIT_BUILD)/methods.o: $(PROGRAM_WAIT_BUILD)/methods.cpp $(PROGRAM_WAIT_DIR)/program-wait.mk $(PROGRAM_WAIT_HEADERS) $(SDK_HEADERS) | program-wait-build-inputs
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

$(PROGRAM_WAIT_BUILD)/errno.o: $(PROGRAM_WAIT_CLANG_BUILD)/source/llvm/lib/Support/Errno.cpp $(PROGRAM_WAIT_DIR)/program-wait.mk $(SDK_HEADERS) | program-wait-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

$(PROGRAM_WAIT_BUILD)/probe.wasm: $(PROGRAM_WAIT_BUILD)/probe.o $(PROGRAM_WAIT_BUILD)/methods.o $(PROGRAM_WAIT_BUILD)/errno.o $(LOCKS_BUILD)/crt.o $(PROGRAM_WAIT_CLANG_BUILD)/cxx/lib/libc++.a $(PROGRAM_WAIT_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(PROGRAM_WAIT_DIR)/program-wait.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(PROGRAM_WAIT_BUILD)/probe.o" "$(PROGRAM_WAIT_BUILD)/methods.o" \
		"$(PROGRAM_WAIT_BUILD)/errno.o" "$(LOCKS_BUILD)/crt.o" \
		"$(PROGRAM_WAIT_CLANG_BUILD)/cxx/lib/libc++.a" "$(PROGRAM_WAIT_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-program-wait: install-libc aux-test-runner program-wait-fixture
	bash "$(PROGRAM_WAIT_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(PROGRAM_WAIT_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(PROGRAM_WAIT_BUILD)/probe.d $(PROGRAM_WAIT_BUILD)/methods.d $(PROGRAM_WAIT_BUILD)/errno.d
