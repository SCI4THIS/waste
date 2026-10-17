# Compiler source compatibility only; no guest runtime behavior is changed.
THREAD_SAFETY_DIR := $(REPO_ROOT)/src/system-tests/clang/thread-safety
THREAD_SAFETY_NAMES := ThreadSafety ThreadSafetyCommon ThreadSafetyLogical ThreadSafetyTIL
THREAD_SAFETY_RULE := tools/clang/lib/Analysis/CMakeFiles/obj.clangAnalysis.dir

.PHONY: thread-safety-source
thread-safety-source: | module-cache-build-inputs
	$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(THREAD_SAFETY_RULE)/build.make $(THREAD_SAFETY_RULE)/depend
	@set -eu; for source in $(THREAD_SAFETY_NAMES); do \
		$(MAKE) -C "$(MODULE_CACHE_GUEST)" -f $(THREAD_SAFETY_RULE)/build.make \
			"$(THREAD_SAFETY_RULE)/$$source.cpp.obj"; \
	done
	@set -eu; for source in $(THREAD_SAFETY_NAMES); do \
		for profile in native native-threads guest-threads; do \
			flag=; case "$$profile" in native-threads) flag=-DWASTE_TEST_NATIVE_THREADS;; guest-threads) flag=-DWASTE_TEST_GUEST_THREADS;; esac; \
			$(MODULE_CACHE_CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
				$$flag -fno-exceptions -fno-rtti \
				-isystem "$(MODULE_CACHE_CLANG_BUILD)/native/include" \
				-isystem "$(MODULE_CACHE_GUEST)/include" \
				-isystem "$(MODULE_CACHE_SOURCE)/llvm/include" \
				-isystem "$(MODULE_CACHE_SOURCE)/clang/include" \
				-isystem "$(MODULE_CACHE_GUEST)/tools/clang/include" \
				-DWASTE_CHECK_SOURCE=\"$(MODULE_CACHE_SOURCE)/clang/lib/Analysis/$$source.cpp\" \
				-fsyntax-only "$(THREAD_SAFETY_DIR)/native.cpp"; \
		done; \
	done
