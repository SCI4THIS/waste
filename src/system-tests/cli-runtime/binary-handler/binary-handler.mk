BINARY_HANDLER_DIR := $(REPO_ROOT)/src/system-tests/cli-runtime/binary-handler
.PHONY: test-binary-handler
test-binary-handler: install-libc aux-test-runner
	bash "$(BINARY_HANDLER_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(abspath $(BUILD_DIR))/cli-runtime/binary-handler" "$(abspath $(WASM_RUNNER))"
