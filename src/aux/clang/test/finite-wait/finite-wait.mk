# Real LLVMSupport archive, production CRT and shared libc/C++ providers.
FINITE_WAIT_DIR := $(REPO_ROOT)/src/aux/clang/test/finite-wait
FINITE_WAIT_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/finite-wait
FINITE_WAIT_CLANG_BUILD := $(REPO_ROOT)/build/aux/clang
.PHONY: finite-wait-fixture finite-wait-build-inputs test-finite-wait test-finite-wait-controlled
finite-wait-fixture: $(FINITE_WAIT_BUILD)/probe.wasm $(FINITE_WAIT_BUILD)/controlled.wasm
finite-wait-build-inputs:
	$(MAKE) -C ../aux clang-support CLANG_JOBS=1

$(FINITE_WAIT_BUILD)/probe.o: $(FINITE_WAIT_DIR)/probe.cpp $(FINITE_WAIT_DIR)/finite-wait.mk $(SDK_HEADERS) | finite-wait-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -c "$<" -o "$@"
$(FINITE_WAIT_BUILD)/controlled.o: $(FINITE_WAIT_DIR)/probe.cpp $(FINITE_WAIT_DIR)/finite-wait.mk $(SDK_HEADERS) | finite-wait-build-inputs
	mkdir -p "$(@D)"
	$(LOCKS_CXX) $(LOCKS_CXXFLAGS) -DWASTE_FINITE_WAIT_CONTROLLED -c "$<" -o "$@"
$(FINITE_WAIT_BUILD)/%.wasm: $(FINITE_WAIT_BUILD)/%.o $(PROGRAM_EXECUTION_SUPPORT) $(LOCKS_BUILD)/crt.o $(FINITE_WAIT_CLANG_BUILD)/guest/lib/libLLVMDemangle.a $(FINITE_WAIT_CLANG_BUILD)/cxx/lib/libc++.a $(FINITE_WAIT_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(FINITE_WAIT_DIR)/finite-wait.mk
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table --experimental-pic \
		--unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__heap_base --export=__data_end --global-base=327680 --table-base=1024 \
		"$(FINITE_WAIT_BUILD)/$*.o" "$(LOCKS_BUILD)/crt.o" "$(PROGRAM_EXECUTION_SUPPORT)" \
		"$(FINITE_WAIT_CLANG_BUILD)/guest/lib/libLLVMDemangle.a" \
		"$(FINITE_WAIT_CLANG_BUILD)/cxx/lib/libc++.a" "$(FINITE_WAIT_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

test-finite-wait: install-libc aux-test-runner finite-wait-fixture
	bash "$(FINITE_WAIT_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(FINITE_WAIT_BUILD)" "$(abspath $(WASM_RUNNER))"
test-finite-wait-controlled: install-libc finite-wait-fixture
	$(MAKE) -C ../cli-rt guest-session guest-session-sanitize
	$(MAKE) -C ../html-rt wast-browser
	cd "$(REPO_ROOT)" && ASAN_OPTIONS="$${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=1}" \
		$(PYTHON) src/engine/test/guest-session/guest-session-check.py --scenario clang-finite-wait --scenario clang-finite-saturation \
		--native "$(REPO_ROOT)/build/cli-rt/private/guest-session-sanitize" \
		--vfs-root "$(VFS_ROOT)" --wasm "$(REPO_ROOT)/build/html-rt/waste-wast.wasm"
-include $(FINITE_WAIT_BUILD)/probe.d $(FINITE_WAIT_BUILD)/controlled.d
