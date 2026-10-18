# Real C++ constructors and current archives through the production CRT.
STARTUP_DIR := $(REPO_ROOT)/src/aux/clang/test/startup
STARTUP_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/startup
STARTUP_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
.PHONY: startup-fixture test-startup
startup-fixture: $(STARTUP_BUILD)/probe.wasm $(STARTUP_BUILD)/probe-two.wasm

$(STARTUP_BUILD)/probe.o: $(STARTUP_DIR)/probe.cpp $(STARTUP_DIR)/startup.mk $(SDK_HEADERS) | locks-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"

$(STARTUP_BUILD)/probe-two.o: $(STARTUP_DIR)/probe.cpp $(STARTUP_DIR)/startup.mk $(SDK_HEADERS) | locks-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -DWASTE_MAIN_TWO_ARGS -c "$<" -o "$@"

$(STARTUP_BUILD)/crt-two.o: $(REPO_ROOT)/src/aux/libc/waste-crt.c $(STARTUP_DIR)/startup.mk $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(LOCKS_CC) $(LOCKS_CFLAGS) -std=c11 -DWASTE_SHARED_LIBC_IMPORTS -DWASTE_MAIN_TWO_ARGS -c "$<" -o "$@"

$(STARTUP_BUILD)/probe.wasm: $(STARTUP_BUILD)/probe.o $(LOCKS_BUILD)/crt.o
$(STARTUP_BUILD)/probe-two.wasm: $(STARTUP_BUILD)/probe-two.o $(STARTUP_BUILD)/crt-two.o

$(STARTUP_BUILD)/probe.wasm $(STARTUP_BUILD)/probe-two.wasm: $(STARTUP_CLANG_BUILD)/cxx/lib/libc++.a $(STARTUP_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(STARTUP_DIR)/startup.mk
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main --export=__stack_pointer \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		$(foreach object,$(filter %.o,$^),"$(object)") \
		"$(STARTUP_CLANG_BUILD)/cxx/lib/libc++.a" "$(STARTUP_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-startup: install-libc aux-test-runner startup-fixture
	bash "$(STARTUP_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(STARTUP_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(STARTUP_BUILD)/probe.d $(STARTUP_BUILD)/probe-two.d
