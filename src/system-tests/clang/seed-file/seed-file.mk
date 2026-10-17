# Actual first-line layout seed reader and complete invocation source gate.
SEED_FILE_DIR := $(REPO_ROOT)/src/system-tests/clang/seed-file
SEED_FILE_BUILD := $(abspath $(BUILD_DIR))/clang/seed-file
SEED_FILE_HEADER := $(REPO_ROOT)/src/aux/clang/include/clang/Frontend/WasteSeedFile.h
SEED_FILE_INPUTS := $(SEED_FILE_DIR)/seed-file.mk $(SEED_FILE_HEADER) $(REPO_ROOT)/src/aux/clang/include/clang/Frontend/WasteFileInput.h $(SDK_HEADERS) $(SEED_FILE_DIR)/cases.h $(SEED_FILE_DIR)/observations.h
SEED_FILE_RULE := tools/clang/lib/Frontend/CMakeFiles/obj.clangFrontend.dir

.PHONY: seed-file-fixture seed-file-source test-seed-file
seed-file-fixture: $(SEED_FILE_BUILD)/probe.wasm $(SEED_FILE_BUILD)/reference.bin

$(SEED_FILE_BUILD)/probe.o: $(SEED_FILE_DIR)/probe.cpp $(SEED_FILE_INPUTS)
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -c "$<" -o "$@"

$(SEED_FILE_BUILD)/probe.wasm: $(SEED_FILE_BUILD)/probe.o $(VFS_ROOT)/usr/lib/waste/cc/crt1.o \
	$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a $(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a \
	$(VFS_ROOT)/lib/libc.so.wasm $(REPO_ROOT)/src/aux/libc/tools/shared_libc.py $(SEED_FILE_INPUTS)
	$(WASM_LD) --fatal-warnings --no-entry --gc-sections --import-memory --import-table \
		--experimental-pic --unresolved-symbols=import-dynamic --export=_start --export=main \
		--export=__stack_pointer --export=__heap_base --export=__data_end \
		--global-base=327680 --table-base=1024 \
		"$(VFS_ROOT)/usr/lib/waste/cc/crt1.o" "$(SEED_FILE_BUILD)/probe.o" \
		"$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++.a" "$(MODULE_CACHE_CLANG_BUILD)/cxx/lib/libc++abi.a" -o "$@.raw"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" \
		--provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$@.raw" --output "$@"
	$(PYTHON) "$(REPO_ROOT)/src/aux/libc/tools/shared_libc.py" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"

$(SEED_FILE_BUILD)/reference: $(SEED_FILE_DIR)/reference.cpp $(SEED_FILE_DIR)/cases.h $(SEED_FILE_DIR)/observations.h $(SEED_FILE_DIR)/seed-file.mk
	mkdir -p "$(@D)"
	$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror "$<" -o "$@"

$(SEED_FILE_BUILD)/reference.bin: $(SEED_FILE_BUILD)/reference
	mkdir -p "$(SEED_FILE_BUILD)/reference-inputs"
	"$<" "$(SEED_FILE_BUILD)/reference-inputs" > "$@"

seed-file-source:
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(SEED_FILE_RULE)/build.make $(SEED_FILE_RULE)/depend
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(SEED_FILE_RULE)/build.make $(SEED_FILE_RULE)/CompilerInvocation.cpp.obj
	@set -eu; for source in CompilerInvocation; do for profile in native native-threads guest-threads; do \
		flag=; case "$$profile" in native-threads) flag=-DWASTE_TEST_NATIVE_THREADS;; guest-threads) flag=-DWASTE_TEST_GUEST_THREADS;; esac; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_GUEST)/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" -isystem "$(MODULE_CACHE_SOURCE)/clang/include" \
			-isystem "$(MODULE_CACHE_GUEST)/tools/clang/include" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/clang/lib/Frontend/$$source.cpp\" \
			-fsyntax-only "$(SEED_FILE_DIR)/native.cpp"; \
	done; done

test-seed-file: seed-file-fixture aux-test-runner
	bash "$(SEED_FILE_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" "$(SEED_FILE_BUILD)" "$(abspath $(WASM_RUNNER))"

-include $(SEED_FILE_BUILD)/probe.d
