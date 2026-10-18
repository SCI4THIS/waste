# Actual LLVM DWARF verifier diagnostic owner and complete source gates.
DWARF_VERIFIER_DIR := $(REPO_ROOT)/src/aux/clang/test/dwarf-verifier
DWARF_VERIFIER_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/dwarf-verifier
DWARF_VERIFIER_INPUTS := $(DWARF_VERIFIER_DIR)/dwarf-verifier.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch
DWARF_VERIFIER_OBJECTS := $(DWARF_VERIFIER_BUILD)/probe.o $(DWARF_VERIFIER_BUILD)/verifier.o $(MODULE_CACHE_BUILD)/crt.o

.PHONY: dwarf-verifier-fixture dwarf-verifier-native-source dwarf-verifier-guest-source test-dwarf-verifier
dwarf-verifier-fixture: $(DWARF_VERIFIER_BUILD)/probe.wasm

$(DWARF_VERIFIER_BUILD)/probe.o: $(DWARF_VERIFIER_DIR)/probe.cpp $(DWARF_VERIFIER_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter -c "$<" -o "$@"

$(DWARF_VERIFIER_BUILD)/verifier.o: $(DWARF_VERIFIER_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter \
		-c "$(MODULE_CACHE_SOURCE)/llvm/lib/DebugInfo/DWARF/DWARFVerifier.cpp" -o "$@"

$(DWARF_VERIFIER_BUILD)/probe.wasm: $(DWARF_VERIFIER_OBJECTS) $(MODULE_CACHE_SUPPORT) \
	$(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a $(VFS_ROOT)/lib/libc.so.wasm \
	$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start \
		--export=main --export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		$(DWARF_VERIFIER_OBJECTS) "$(MODULE_CACHE_SUPPORT)" \
		"$(MODULE_CACHE_GUEST)/lib/libLLVMDemangle.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

dwarf-verifier-native-source: | module-cache-build-inputs
	for profile in native threaded; do \
		flag=; test "$$profile" != threaded || flag=-DWASTE_TEST_THREADED_BRANCH; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" \
			-I "$(MODULE_CACHE_SOURCE)/llvm/lib/DebugInfo/DWARF" \
			-fsyntax-only "$(DWARF_VERIFIER_DIR)/native.cpp" || exit; \
	done

# Check the actual generated PCH/object rule for the complete verifier.
dwarf-verifier-guest-source: | module-cache-build-inputs
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/DebugInfo/DWARF/CMakeFiles/LLVMDebugInfoDWARF.dir/build.make \
		lib/DebugInfo/DWARF/CMakeFiles/LLVMDebugInfoDWARF.dir/depend -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f lib/DebugInfo/DWARF/CMakeFiles/LLVMDebugInfoDWARF.dir/build.make \
		lib/DebugInfo/DWARF/CMakeFiles/LLVMDebugInfoDWARF.dir/DWARFVerifier.cpp.obj -j1

test-dwarf-verifier: install-libc aux-test-runner dwarf-verifier-fixture
	bash "$(DWARF_VERIFIER_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(DWARF_VERIFIER_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(wildcard $(DWARF_VERIFIER_BUILD)/*.d)
