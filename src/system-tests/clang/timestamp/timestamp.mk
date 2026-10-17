# Clang's real __TIMESTAMP__ formatter and its complete macro expansion owner.
TIMESTAMP_DIR := $(REPO_ROOT)/src/system-tests/clang/timestamp
TIMESTAMP_BUILD := $(abspath $(BUILD_DIR))/clang/timestamp
TIMESTAMP_HEADER := $(REPO_ROOT)/src/aux/clang/include/clang/Lex/WasteTimestamp.h
TIMESTAMP_INPUTS := $(TIMESTAMP_DIR)/timestamp.mk $(TIMESTAMP_HEADER) $(REPO_ROOT)/src/aux/clang/include/clang/Lex/WastePragma.h $(REPO_ROOT)/src/aux/clang/include/llvm/Support/WasteBackoff.h $(SDK_HEADERS) $(TIMESTAMP_DIR)/cases.h
TIMESTAMP_RULE := tools/clang/lib/Lex/CMakeFiles/obj.clangLex.dir

.PHONY: timestamp-fixture timestamp-source test-timestamp
timestamp-fixture: $(TIMESTAMP_BUILD)/probe.wasm $(TIMESTAMP_BUILD)/reference.bin

$(TIMESTAMP_BUILD)/probe.o: $(TIMESTAMP_DIR)/probe.cpp $(TIMESTAMP_INPUTS)
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -c "$<" -o "$@"

$(TIMESTAMP_BUILD)/probe.wasm: $(TIMESTAMP_BUILD)/probe.o $(VFS_ROOT)/usr/lib/waste/cc/crt1.o \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(TIMESTAMP_INPUTS)
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		"$(VFS_ROOT)/usr/lib/waste/cc/crt1.o" "$(TIMESTAMP_BUILD)/probe.o" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" "$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

$(TIMESTAMP_BUILD)/reference: $(TIMESTAMP_DIR)/reference.cpp $(TIMESTAMP_DIR)/cases.h $(TIMESTAMP_DIR)/timestamp.mk
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror "$<" -o "$@"

$(TIMESTAMP_BUILD)/reference.bin: $(TIMESTAMP_BUILD)/reference
	TZ=UTC "$<" > "$@"

timestamp-source:
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(TIMESTAMP_RULE)/build.make $(TIMESTAMP_RULE)/depend
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(TIMESTAMP_RULE)/build.make $(TIMESTAMP_RULE)/PPMacroExpansion.cpp.obj $(TIMESTAMP_RULE)/Pragma.cpp.obj
	@set -eu; for source in PPMacroExpansion Pragma; do for profile in native native-threads guest-threads; do \
		flag=; case "$$profile" in native-threads) flag=-DWASTE_TEST_NATIVE_THREADS;; guest-threads) flag=-DWASTE_TEST_GUEST_THREADS;; esac; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" -isystem "$(MODULE_CACHE_SOURCE)/clang/include" \
			-isystem "$(MODULE_CACHE_GUEST)/tools/clang/include" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/clang/lib/Lex/$$source.cpp\" \
			-fsyntax-only "$(TIMESTAMP_DIR)/native.cpp"; \
	done; done

test-timestamp: timestamp-fixture aux-test-runner
	bash "$(TIMESTAMP_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(TIMESTAMP_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(TIMESTAMP_BUILD)/probe.d
