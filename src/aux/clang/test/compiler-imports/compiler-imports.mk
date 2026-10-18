# Source-backed coverage of the imports retained by the linked Clang command.
COMPILER_IMPORTS_DIR := $(REPO_ROOT)/src/aux/clang/test/compiler-imports
COMPILER_IMPORTS_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/compiler-imports
COMPILER_IMPORTS_INPUTS := $(COMPILER_IMPORTS_DIR)/compiler-imports.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch
COMPILER_IMPORTS_ARCHIVES := $(addprefix $(MODULE_CACHE_GUEST)/lib/libLLVM,\
	Core.a Remarks.a BitstreamReader.a BinaryFormat.a TargetParser.a Support.a Demangle.a)

.PHONY: compiler-imports-fixture compiler-imports-source test-compiler-imports
compiler-imports-fixture: $(COMPILER_IMPORTS_BUILD)/probe.wasm

$(COMPILER_IMPORTS_BUILD)/probe.o: $(COMPILER_IMPORTS_DIR)/probe.cpp $(COMPILER_IMPORTS_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter -c "$<" -o "$@"

$(COMPILER_IMPORTS_BUILD)/probe.wasm: $(COMPILER_IMPORTS_BUILD)/probe.o \
	$(VFS_ROOT)/usr/lib/waste/cc/crt1.o $(COMPILER_IMPORTS_ARCHIVES) \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		$(COMPILER_IMPORTS_BUILD)/probe.o "$(VFS_ROOT)/usr/lib/waste/cc/crt1.o" \
		$(COMPILER_IMPORTS_ARCHIVES) "$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

compiler-imports-source: | module-cache-build-inputs
	@set -eu; for profile in native native-threads guest-threads; do \
		flag=; case "$$profile" in native-threads) flag=-DWASTE_TEST_NATIVE_THREADS;; guest-threads) flag=-DWASTE_TEST_GUEST_THREADS;; esac; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" -isystem "$(MODULE_CACHE_GUEST)/include" \
			-fsyntax-only "$(COMPILER_IMPORTS_DIR)/probe.cpp"; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" -isystem "$(MODULE_CACHE_GUEST)/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/clang/include" \
			-isystem "$(MODULE_CACHE_GUEST)/tools/clang/include" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/clang/lib/Driver/Job.cpp\" \
			-fsyntax-only "$(COMPILER_IMPORTS_DIR)/native-job.cpp"; \
	done

test-compiler-imports: install-libc aux-test-runner compiler-imports-fixture
	bash "$(COMPILER_IMPORTS_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(COMPILER_IMPORTS_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(wildcard $(COMPILER_IMPORTS_BUILD)/*.d)
