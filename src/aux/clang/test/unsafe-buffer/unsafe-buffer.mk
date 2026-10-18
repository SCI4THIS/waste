# Complete source checks and actual guest-compiler fix-it acceptance.
UNSAFE_BUFFER_DIR := $(REPO_ROOT)/src/aux/clang/test/unsafe-buffer
UNSAFE_BUFFER_BUILD := $(abspath $(SYSTEM_TESTS_BUILD))/clang/unsafe-buffer
UNSAFE_BUFFER_RULE := tools/clang/lib/Analysis/CMakeFiles/obj.clangAnalysis.dir

.PHONY: unsafe-buffer-source unsafe-buffer-compiler test-unsafe-buffer
unsafe-buffer-source: | module-cache-build-inputs
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(UNSAFE_BUFFER_RULE)/build.make $(UNSAFE_BUFFER_RULE)/depend
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(UNSAFE_BUFFER_RULE)/build.make $(UNSAFE_BUFFER_RULE)/UnsafeBufferUsage.cpp.obj
	mkdir -p "$(UNSAFE_BUFFER_BUILD)"
	# Also compile the seventh, debug-only stream owner with the guest headers.
	$(MODULE_CACHE_CXX) $(MODULE_CACHE_CXXFLAGS) -Wno-unused-parameter -UNDEBUG \
		-MF "$(UNSAFE_BUFFER_BUILD)/guest-debug.d" -fsyntax-only \
		"$(MODULE_CACHE_SOURCE)/clang/lib/Analysis/UnsafeBufferUsage.cpp"
	@set -eu; for profile in native native-threads guest-threads; do \
		flag=; case "$$profile" in native-threads) flag=-DWASTE_TEST_NATIVE_THREADS;; guest-threads) flag=-DWASTE_TEST_GUEST_THREADS;; esac; \
		$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
			$$flag -fno-exceptions -fno-rtti \
			-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
			-isystem "$(MODULE_CACHE_GUEST)/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" \
			-isystem "$(MODULE_CACHE_SOURCE)/clang/include" \
			-isystem "$(MODULE_CACHE_GUEST)/tools/clang/include" \
			-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/clang/lib/Analysis/UnsafeBufferUsage.cpp\" \
			-fsyntax-only "$(UNSAFE_BUFFER_DIR)/native.cpp"; \
	done

unsafe-buffer-compiler:
	$(MAKE) -C ../aux install-clang

test-unsafe-buffer: unsafe-buffer-compiler aux-test-runner
	bash "$(UNSAFE_BUFFER_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(UNSAFE_BUFFER_BUILD)" "$(abspath $(WASM_RUNNER))"
