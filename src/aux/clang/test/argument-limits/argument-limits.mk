# Actual staged argument-fit overloads; whole Program.cpp remains a gate.
ARGUMENT_LIMITS_DIR := $(REPO_ROOT)/src/aux/clang/test/argument-limits
ARGUMENT_LIMITS_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/argument-limits
ARGUMENT_LIMITS_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
ARGUMENT_LIMITS_HEADERS := $(REPO_ROOT)/src/aux/clang/include/llvm/Support/WasteProgram.h

.PHONY: argument-limits-fixture argument-limits-build-inputs test-argument-limits
argument-limits-fixture: $(ARGUMENT_LIMITS_BUILD)/probe.wasm
argument-limits-build-inputs:
	$(MAKE) -C ../aux "$(ARGUMENT_LIMITS_CLANG_BUILD)/guest/.configured"

$(ARGUMENT_LIMITS_BUILD)/probe.o: $(ARGUMENT_LIMITS_DIR)/probe.cpp $(ARGUMENT_LIMITS_DIR)/argument-limits.mk $(SDK_HEADERS) | argument-limits-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

$(ARGUMENT_LIMITS_BUILD)/methods.cpp: $(ARGUMENT_LIMITS_DIR)/prepare-arguments.py $(REPO_ROOT)/src/aux/clang/clang-waste.patch $(ARGUMENT_LIMITS_CLANG_BUILD)/source/llvm/lib/Support/Unix/Program.inc $(ARGUMENT_LIMITS_CLANG_BUILD)/source/llvm/lib/Support/Program.cpp | argument-limits-build-inputs
	$(PYTHON) "$<" "$(ARGUMENT_LIMITS_CLANG_BUILD)/source" "$@"

$(ARGUMENT_LIMITS_BUILD)/methods.o: $(ARGUMENT_LIMITS_BUILD)/methods.cpp $(ARGUMENT_LIMITS_DIR)/argument-limits.mk $(ARGUMENT_LIMITS_HEADERS) $(SDK_HEADERS) | argument-limits-build-inputs
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

$(ARGUMENT_LIMITS_BUILD)/probe.wasm: $(ARGUMENT_LIMITS_BUILD)/probe.o $(ARGUMENT_LIMITS_BUILD)/methods.o $(LOCKS_BUILD)/crt.o $(ARGUMENT_LIMITS_CLANG_BUILD)/cxx/lib/libc++.a $(ARGUMENT_LIMITS_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(ARGUMENT_LIMITS_DIR)/argument-limits.mk
	$(WASM_LD) --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(ARGUMENT_LIMITS_BUILD)/probe.o" "$(ARGUMENT_LIMITS_BUILD)/methods.o" \
		"$(LOCKS_BUILD)/crt.o" \
		"$(ARGUMENT_LIMITS_CLANG_BUILD)/cxx/lib/libc++.a" "$(ARGUMENT_LIMITS_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-argument-limits: install-libc aux-test-runner argument-limits-fixture
	bash "$(ARGUMENT_LIMITS_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(ARGUMENT_LIMITS_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(ARGUMENT_LIMITS_BUILD)/probe.d $(ARGUMENT_LIMITS_BUILD)/methods.d
