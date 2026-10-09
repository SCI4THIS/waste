# Shared transport for package-owned WAST tests; no host-side assertions.
WASM_RUNNER ?= $(REPO_ROOT)/build/cli-rt/wasm
.PHONY: aux-test-runner
aux-test-runner:
	$(MAKE) -C "$(REPO_ROOT)/src/cli-rt" BUILD_DIR="$(REPO_ROOT)/build/cli-rt" \
		"$(abspath $(WASM_RUNNER))"
