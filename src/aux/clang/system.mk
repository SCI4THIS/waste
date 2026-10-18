# Clang topic aggregator for system tests. Each topic owns its own .mk file.
# Included from clang.mk so topic recipes/vars resolve inside the aux build.

SYSTEM_TESTS_BUILD ?= $(REPO_ROOT)/build/system-tests
SDK_TOOLS := $(REPO_ROOT)/src/html-rt/tools
SDK_DIR := $(REPO_ROOT)/src/aux/libc/test/sdk
SDK_BUILD := $(SYSTEM_TESTS_BUILD)/guest-sdk
SYSROOT_DIR ?= $(SDK_BUILD)/sysroot
SDK_CC := $(SYSROOT_DIR)/bin/waste-wasm-clang

.PHONY: test-clang-system
test-clang-system: install-clang aux-test-runner
	bash "$(REPO_ROOT)/src/aux/run-wast-tests.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" system-clang \
		"$(REPO_ROOT)/src/vfs/root/test/system/clang" "$(SYSTEM_TESTS_BUILD)/clang" "$(abspath $(WASM_RUNNER))" \
		"$(REPO_ROOT)/src/aux/clang/test/setup.sh"

include $(REPO_ROOT)/src/aux/clang/test/stdio/stdio.mk
include $(REPO_ROOT)/src/aux/clang/test/classification/classification.mk
include $(REPO_ROOT)/src/aux/clang/test/endian/endian.mk
include $(REPO_ROOT)/src/aux/clang/test/wide/wide.mk
include $(REPO_ROOT)/src/aux/clang/test/wide-format/wide-format.mk
include $(REPO_ROOT)/src/aux/clang/test/filesystem/filesystem.mk
include $(REPO_ROOT)/src/aux/clang/test/mutex/mutex.mk
include $(REPO_ROOT)/src/aux/clang/test/logb/logb.mk
include $(REPO_ROOT)/src/aux/clang/test/synchronization/synchronization.mk
include $(REPO_ROOT)/src/aux/clang/test/thread-pool/thread-pool.mk
include $(REPO_ROOT)/src/aux/clang/test/checkpoint/checkpoint.mk
include $(REPO_ROOT)/src/aux/clang/test/backoff/backoff.mk
include $(REPO_ROOT)/src/aux/clang/test/lock-owner/lock-owner.mk
include $(REPO_ROOT)/src/aux/clang/test/managed-static/managed-static.mk
include $(REPO_ROOT)/src/aux/clang/test/mustache/mustache.mk
include $(REPO_ROOT)/src/aux/clang/test/parallel/parallel.mk
include $(REPO_ROOT)/src/aux/clang/test/time-profiler/time-profiler.mk
include $(REPO_ROOT)/src/aux/clang/test/module-cache/module-cache.mk
include $(REPO_ROOT)/src/aux/clang/test/module-tasks/module-tasks.mk
include $(REPO_ROOT)/src/aux/clang/test/lld-diagnostics/lld-diagnostics.mk
include $(REPO_ROOT)/src/aux/clang/test/lld-filesystem/lld-filesystem.mk
include $(REPO_ROOT)/src/aux/clang/test/lld-link/lld-link.mk
include $(REPO_ROOT)/src/aux/clang/test/lto-errors/lto-errors.mk
include $(REPO_ROOT)/src/aux/clang/test/target-host/target-host.mk
include $(REPO_ROOT)/src/aux/clang/test/dwarf-verifier/dwarf-verifier.mk
include $(REPO_ROOT)/src/aux/clang/test/dwarf-context/dwarf-context.mk
include $(REPO_ROOT)/src/aux/clang/test/gsym/gsym.mk
include $(REPO_ROOT)/src/aux/clang/test/cfg-format/cfg-format.mk
include $(REPO_ROOT)/src/aux/clang/test/inlining/inlining.mk
include $(REPO_ROOT)/src/aux/clang/test/folding/folding.mk
include $(REPO_ROOT)/src/aux/clang/test/code-layout/code-layout.mk
include $(REPO_ROOT)/src/aux/clang/test/assignment-format/assignment-format.mk
include $(REPO_ROOT)/src/aux/clang/test/shadow-callbacks/shadow-callbacks.mk
include $(REPO_ROOT)/src/aux/clang/test/memprof-format/memprof-format.mk
include $(REPO_ROOT)/src/aux/clang/test/raw-stream/raw-stream.mk
include $(REPO_ROOT)/src/aux/clang/test/platform/platform.mk
include $(REPO_ROOT)/src/aux/clang/test/executable-path/executable-path.mk
include $(REPO_ROOT)/src/aux/clang/test/symlink/symlink.mk
include $(REPO_ROOT)/src/aux/clang/test/hardlink/hardlink.mk
include $(REPO_ROOT)/src/aux/clang/test/accounts/accounts.mk
include $(REPO_ROOT)/src/aux/clang/test/advice/advice.mk
include $(REPO_ROOT)/src/aux/clang/test/realpath/realpath.mk
include $(REPO_ROOT)/src/aux/clang/test/locks/locks.mk
include $(REPO_ROOT)/src/aux/clang/test/timed-locks/timed-locks.mk
include $(REPO_ROOT)/src/aux/clang/test/process/process.mk
include $(REPO_ROOT)/src/aux/clang/test/exec-wait/exec-wait.mk
include $(REPO_ROOT)/src/aux/clang/test/program-wait/program-wait.mk
include $(REPO_ROOT)/src/aux/clang/test/argument-limits/argument-limits.mk
include $(REPO_ROOT)/src/aux/clang/test/session/session.mk
include $(REPO_ROOT)/src/aux/clang/test/signals/signals.mk
include $(REPO_ROOT)/src/aux/clang/test/watchdog/watchdog.mk
include $(REPO_ROOT)/src/aux/clang/test/program-execution/program-execution.mk
include $(REPO_ROOT)/src/aux/clang/test/child-deadline/child-deadline.mk
include $(REPO_ROOT)/src/aux/clang/test/finite-wait/finite-wait.mk
include $(REPO_ROOT)/src/aux/clang/test/crash-recovery/crash-recovery.mk
include $(REPO_ROOT)/src/aux/clang/test/startup/startup.mk
include $(REPO_ROOT)/src/aux/clang/test/shutdown/shutdown.mk
include $(REPO_ROOT)/src/aux/clang/test/timestamp/timestamp.mk
include $(REPO_ROOT)/src/aux/clang/test/coverage-format/coverage-format.mk
include $(REPO_ROOT)/src/aux/clang/test/dylib-uuid/dylib-uuid.mk
include $(REPO_ROOT)/src/aux/clang/test/issue-hash/issue-hash.mk
include $(REPO_ROOT)/src/aux/clang/test/thread-safety/thread-safety.mk
include $(REPO_ROOT)/src/aux/clang/test/unsafe-buffer/unsafe-buffer.mk
include $(REPO_ROOT)/src/aux/clang/test/seed-file/seed-file.mk
include $(REPO_ROOT)/src/aux/clang/test/layout-override/layout-override.mk
include $(REPO_ROOT)/src/aux/clang/test/spirv-name/spirv-name.mk
include $(REPO_ROOT)/src/aux/clang/test/compiler-imports/compiler-imports.mk

# These topic gates share the same LLVM archives and identical presence checks.
$(sort $(ISSUE_HASH_CLANG_ARCHIVES) $(LAYOUT_OVERRIDE_CLANG_ARCHIVES)):
	test -s "$@"
