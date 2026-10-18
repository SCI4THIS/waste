# Full LLVM Process.cpp, with runtime expectations in WAST.
PROCESS_DIR := $(REPO_ROOT)/src/aux/clang/test/process
PROCESS_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/process
PROCESS_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
PROCESS_CXXFLAGS := $(LOCKS_CXXFLAGS) -Wno-error=cpp
.PHONY: process-fixture process-build-inputs test-clang-process
process-fixture: $(PROCESS_BUILD)/probe.wasm
process-build-inputs:
	$(MAKE) -C ../aux "$(PROCESS_CLANG_BUILD)/guest/.configured"

$(PROCESS_BUILD)/probe.o: $(PROCESS_DIR)/probe.cpp $(PROCESS_DIR)/process.mk $(SDK_HEADERS) $(REPO_ROOT)/src/aux/clang/guest.cmake | process-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(PROCESS_CXXFLAGS) -c "$<" -o "$@"

$(PROCESS_BUILD)/process.o: $(PROCESS_DIR)/process.mk $(SDK_HEADERS) $(REPO_ROOT)/src/aux/clang/guest.cmake $(PROCESS_CLANG_BUILD)/source/llvm/lib/Support/Process.cpp | process-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(PROCESS_CXXFLAGS) -c "$(PROCESS_CLANG_BUILD)/source/llvm/lib/Support/Process.cpp" -o "$@"

$(PROCESS_BUILD)/probe.wasm: $(PROCESS_BUILD)/probe.o $(PROCESS_BUILD)/process.o $(LOCKS_BUILD)/crt.o $(PROCESS_CLANG_BUILD)/cxx/lib/libc++.a $(PROCESS_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(PROCESS_DIR)/process.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(PROCESS_BUILD)/probe.o" "$(PROCESS_BUILD)/process.o" "$(LOCKS_BUILD)/crt.o" \
		"$(PROCESS_CLANG_BUILD)/cxx/lib/libc++.a" "$(PROCESS_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-clang-process: install-libc aux-test-runner process-fixture
	bash "$(PROCESS_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(PROCESS_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(PROCESS_BUILD)/probe.d $(PROCESS_BUILD)/process.d
