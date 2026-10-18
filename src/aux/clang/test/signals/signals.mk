# Actual LLVM ordinary signal methods and public libc calls; WAST owns assertions.
SIGNALS_DIR := $(REPO_ROOT)/src/aux/clang/test/signals
SIGNALS_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/signals
SIGNALS_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang

.PHONY: signals-fixture signals-build-inputs test-signals
signals-fixture: $(SIGNALS_BUILD)/probe.wasm
signals-build-inputs:
	$(MAKE) -C ../aux "$(SIGNALS_CLANG_BUILD)/guest/.configured"

$(SIGNALS_BUILD)/probe.o: $(SIGNALS_DIR)/probe.cpp $(SIGNALS_DIR)/signals.mk $(SDK_HEADERS) | signals-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

$(SIGNALS_BUILD)/probe.wasm: $(SIGNALS_BUILD)/probe.o $(SIGNALS_BUILD)/methods.o $(SIGNALS_BUILD)/managed.o $(LOCKS_BUILD)/crt.o $(SIGNALS_CLANG_BUILD)/cxx/lib/libc++.a $(SIGNALS_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(SIGNALS_DIR)/signals.mk
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(SIGNALS_BUILD)/probe.o" "$(SIGNALS_BUILD)/methods.o" "$(SIGNALS_BUILD)/managed.o" "$(LOCKS_BUILD)/crt.o" \
		"$(SIGNALS_CLANG_BUILD)/cxx/lib/libc++.a" "$(SIGNALS_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-signals: install-libc aux-test-runner signals-fixture
	bash "$(SIGNALS_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(SIGNALS_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(SIGNALS_BUILD)/probe.d

$(SIGNALS_BUILD)/methods.cpp: $(SIGNALS_DIR)/prepare-signals.py $(SIGNALS_CLANG_BUILD)/source/llvm/lib/Support/Signals.cpp $(SIGNALS_CLANG_BUILD)/source/llvm/lib/Support/Unix/Signals.inc | signals-build-inputs
	$(PYTHON) "$<" "$(SIGNALS_CLANG_BUILD)/source" "$@"

$(SIGNALS_BUILD)/methods.o: $(SIGNALS_BUILD)/methods.cpp $(SIGNALS_DIR)/signals.mk $(SDK_HEADERS) | signals-build-inputs
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -Wno-unused-function -Wno-unused-variable -c "$<" -o "$@"

$(SIGNALS_BUILD)/managed.o: $(SIGNALS_CLANG_BUILD)/source/llvm/lib/Support/ManagedStatic.cpp $(SIGNALS_DIR)/signals.mk $(SDK_HEADERS) | signals-build-inputs
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

-include $(SIGNALS_BUILD)/methods.d $(SIGNALS_BUILD)/managed.d
