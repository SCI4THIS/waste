.PHONY: clipboard install-clipboard test-clipboard
all: clipboard
clipboard: $(BUILD_DIR)/clipboard/clipboard.wasm
install-clipboard: clipboard

$(BUILD_DIR)/clipboard/clipboard.o: $(AUX_DIR)/clipboard/clipboard.c $(REPO_ROOT)/src/config.h $(AUX_DIR)/libc-imports.h $(AUX_DIR)/clipboard/clipboard.mk $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) -Wall -Wextra -Werror \
		-include "$(AUX_DIR)/libc-imports.h" -c "$<" -o "$@"

$(BUILD_DIR)/clipboard/clipboard.wasm: $(BUILD_DIR)/clipboard/notice-crt.o $(BUILD_DIR)/clipboard/clipboard.o
	$(WASM_LD) $(LDFLAGS) $(WASM_LDFLAGS) -o "$@" $(foreach object,$^,"$(object)")

test-clipboard: install-clipboard aux-test-runner
	bash "$(AUX_DIR)/run-wast-tests.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" clipboard \
		"$(AUX_DIR)/clipboard/test/system" "$(abspath $(BUILD_DIR)/clipboard/tests)" \
		"$(abspath $(WASM_RUNNER))" "$(AUX_DIR)/clipboard/test/system/native-setup.sh"
