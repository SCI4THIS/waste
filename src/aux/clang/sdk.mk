# Resource headers are target-independent; use the already configured native
# generator tree so this can run alongside guest compilation without racing it.
CLANG_SDK_STAGE := $(CLANG_BUILD)/sdk
CLANG_SDK_INPUTS := $(CLANG_DIR)/stage-sdk.sh $(CLANG_DIR)/clang.cfg.in $(CLANG_DIR)/sdk.mk
CLANG_SDK_CRT := $(CLANG_BUILD)/sdk-crt.o

# User programs keep the plain startup ABI; the tools' own CRT opts into notices.
$(CLANG_SDK_CRT): $(CRT_SOURCE) $(CLANG_DIR)/sdk.mk $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) -DWASTE_MAIN_TWO_ARGS -c "$<" -o "$@"

.PHONY: clang-sdk-files install-clang-sdk
clang: clang-sdk-files
clang-sdk-files: $(CLANG_SDK_STAGE)/.prepared

$(CLANG_SDK_STAGE)/.prepared: $(CLANG_STAGE) $(CLANG_HOST_BUILD)/.configured $(CLANG_SDK_CRT) $(CLANG_SDK_INPUTS)
	bash "$(CLANG_DIR)/stage-sdk.sh" "$(CLANG_CMAKE)" "$(CLANG_HOST_BUILD)" \
		"$(CLANG_SDK_CRT)" "$(CLANG_DIR)/clang.cfg.in" "$(CLANG_SDK_STAGE)" "$(CLANG_SOURCE)/LICENSE.TXT"
	touch "$@"

install-clang-sdk: clang-sdk-files
	flock "$(BUILD_DIR)/.vfs-install.lock" bash -c \
		'mkdir -p "$$2/usr" && cp -a "$$1/usr/." "$$2/usr/"' \
		-- "$(CLANG_SDK_STAGE)" "$(VFS_ROOT)"
