# Actual Clang issue line normalization; complete production source gate.
ISSUE_HASH_DIR := $(REPO_ROOT)/src/aux/clang/test/issue-hash
ISSUE_HASH_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/issue-hash
ISSUE_HASH_INPUTS := $(ISSUE_HASH_DIR)/issue-hash.mk $(SDK_HEADERS) \
	$(REPO_ROOT)/src/aux/clang/clang-waste.patch $(REPO_ROOT)/src/aux/clang/clang.mk \
	$(wildcard $(REPO_ROOT)/src/aux/clang/*.cmake)
ISSUE_HASH_SOURCE_NAMES := IssueHash
ISSUE_HASH_SOURCE_RULE := tools/clang/lib/Analysis/CMakeFiles/obj.clangAnalysis.dir
ISSUE_HASH_SOURCE_OBJECTS := $(addprefix $(MODULE_CACHE_GUEST)/$(ISSUE_HASH_SOURCE_RULE)/,\
	$(addsuffix .cpp.obj,$(ISSUE_HASH_SOURCE_NAMES)))
ISSUE_HASH_SOURCE_STAMP := $(ISSUE_HASH_BUILD)/.sources-checked
ISSUE_HASH_OBJECTS := $(ISSUE_HASH_BUILD)/probe.o $(VFS_ROOT)/usr/lib/waste/cc/crt1.o
ISSUE_HASH_CLANG_ARCHIVES := $(addprefix $(MODULE_CACHE_GUEST)/lib/libclang,Lex.a Basic.a)
ISSUE_HASH_ARCHIVES := $(addprefix $(MODULE_CACHE_GUEST)/lib/libLLVM,\
	TargetParser.a Support.a Demangle.a)

.PHONY: issue-hash-fixture issue-hash-build-inputs issue-hash-guest-source issue-hash-native-source test-issue-hash
issue-hash-fixture: $(ISSUE_HASH_BUILD)/probe.wasm
issue-hash-build-inputs: $(ISSUE_HASH_SOURCE_STAMP)

$(ISSUE_HASH_SOURCE_STAMP): $(ISSUE_HASH_INPUTS) | module-cache-build-inputs
	mkdir -p "$(@D)"
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" clangLex -j1
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(ISSUE_HASH_SOURCE_RULE)/build.make $(ISSUE_HASH_SOURCE_RULE)/depend
	@set -eu; for source in $(ISSUE_HASH_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(ISSUE_HASH_SOURCE_RULE)/build.make \
			"$(ISSUE_HASH_SOURCE_RULE)/$$source.cpp.obj"; \
	done
	touch "$@"

$(ISSUE_HASH_SOURCE_OBJECTS): | issue-hash-build-inputs
	test -s "$@" || $(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(ISSUE_HASH_SOURCE_RULE)/build.make "$(patsubst $(MODULE_CACHE_GUEST)/%,%,$@)"

$(ISSUE_HASH_CLANG_ARCHIVES): | issue-hash-build-inputs

$(ISSUE_HASH_BUILD)/probe.o: $(ISSUE_HASH_DIR)/probe.cpp $(ISSUE_HASH_INPUTS) | issue-hash-build-inputs
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter -I "$(MODULE_CACHE_SOURCE)/clang/lib/Analysis" -c "$<" -o "$@"

$(ISSUE_HASH_BUILD)/probe.wasm: $(ISSUE_HASH_OBJECTS) $(ISSUE_HASH_SOURCE_STAMP) $(ISSUE_HASH_SOURCE_OBJECTS) $(ISSUE_HASH_CLANG_ARCHIVES) $(ISSUE_HASH_ARCHIVES) \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start \
		--export=main --export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		$(ISSUE_HASH_OBJECTS) $(ISSUE_HASH_CLANG_ARCHIVES) $(ISSUE_HASH_ARCHIVES) \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

issue-hash-guest-source: | issue-hash-build-inputs
	@set -eu; for source in $(ISSUE_HASH_SOURCE_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(ISSUE_HASH_SOURCE_RULE)/build.make \
			"$(ISSUE_HASH_SOURCE_RULE)/$$source.cpp.obj"; \
	done

issue-hash-native-source: | module-cache-build-inputs
	for profile in native native-threads guest-threads; do \
		flag=; case "$$profile" in native-threads) flag=-DWASTE_TEST_NATIVE_THREADS;; guest-threads) flag=-DWASTE_TEST_GUEST_THREADS;; esac; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/clang/include" \
			-isystem "$(MODULE_CACHE_GUEST)/tools/clang/include" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/clang/lib/Analysis/IssueHash.cpp\" \
			-fsyntax-only "$(ISSUE_HASH_DIR)/native.cpp" || exit; \
	done

test-issue-hash: aux-test-runner issue-hash-fixture
	bash "$(ISSUE_HASH_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(ISSUE_HASH_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(wildcard $(ISSUE_HASH_BUILD)/*.d)
