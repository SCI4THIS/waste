BINARY_HANDLER_DIR := $(REPO_ROOT)/src/cli-rt/test/binary-handler
.PHONY: test-binary-handler
test-binary-handler: install-libc aux-test-runner
	bash "$(BINARY_HANDLER_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(abspath $(SYSTEM_TESTS_BUILD))/cli-runtime/binary-handler" "$(abspath $(WASM_RUNNER))"
