# End-to-end linking with the actual guest LLD artifact, without installing it.
LLD_LINK_DIR := $(REPO_ROOT)/src/system-tests/clang/lld-link
LLD_LINK_BUILD := $(abspath $(BUILD_DIR))/clang/lld-link
LLD_LINK_TOOL ?= $(REPO_ROOT)/build/aux/clang/wasm-ld.wasm
LLD_LINK_CC ?= clang
LLD_LINK_AR ?= llvm-ar
LLD_LINK_COMMANDS ?= $(LLD_LINK_DIR)/commands.sh

.PHONY: lld-link-fixture test-lld-link
lld-link-fixture: $(LLD_LINK_BUILD)/entry.o $(LLD_LINK_BUILD)/answer.a $(LLD_LINK_BUILD)/answer.bc $(LLD_LINK_BUILD)/answer-bc.a
	test -f "$(LLD_LINK_TOOL)"

$(LLD_LINK_BUILD)/%.o: $(LLD_LINK_DIR)/%.c $(LLD_LINK_DIR)/lld-link.mk
	mkdir -p "$(@D)"
	$(LLD_LINK_CC) --target=wasm32-unknown-unknown -ffreestanding -fno-builtin -nostdlib -O1 -c "$<" -o "$@"

$(LLD_LINK_BUILD)/answer.a: $(LLD_LINK_BUILD)/answer.o
	$(LLD_LINK_AR) rcs "$@" "$<"

$(LLD_LINK_BUILD)/sdk.o: $(LLD_LINK_DIR)/sdk.c $(LLD_LINK_DIR)/lld-link.mk $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(LLD_LINK_CC) --target=wasm32-unknown-unknown -ffreestanding -fno-builtin -nostdlib -fPIC -O1 \
		-nostdinc -isystem "$(VFS_ROOT)/usr/include" -isystem "$(VFS_ROOT)/usr/lib/waste/cc/include" -c "$<" -o "$@"

$(LLD_LINK_BUILD)/sdk-crt.o: $(VFS_ROOT)/usr/lib/waste/cc/crt1.o $(LLD_LINK_DIR)/lld-link.mk
	mkdir -p "$(@D)"
	cp -- "$<" "$@"

$(LLD_LINK_BUILD)/answer.bc: $(LLD_LINK_DIR)/answer.c $(LLD_LINK_DIR)/lld-link.mk
	mkdir -p "$(@D)"
	$(LLD_LINK_CC) --target=wasm32-unknown-unknown -ffreestanding -fno-builtin -nostdlib -O1 -emit-llvm -c "$<" -o "$@"

$(LLD_LINK_BUILD)/answer-bc.a: $(LLD_LINK_BUILD)/answer.bc
	$(LLD_LINK_AR) rcs "$@" "$<"

test-lld-link: lld-link-fixture aux-test-runner
	bash "$(LLD_LINK_DIR)/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" \
		"$(LLD_LINK_BUILD)" "$(abspath $(WASM_RUNNER))" "$(abspath $(LLD_LINK_TOOL))" "$(LLD_LINK_COMMANDS)"

.PHONY: test-lld-link-no-lto
test-lld-link-no-lto:
	$(MAKE) test-lld-link LLD_LINK_TOOL="$(REPO_ROOT)/build/aux/clang/wasm-ld-no-lto.wasm" \
		LLD_LINK_COMMANDS="$(LLD_LINK_DIR)/no-lto-commands.sh"

.PHONY: lld-sdk-fixture test-lld-sdk
lld-sdk-fixture: $(LLD_LINK_BUILD)/sdk.o $(LLD_LINK_BUILD)/sdk-crt.o $(LLD_LINK_BUILD)/bad-signature.o

test-lld-sdk: lld-sdk-fixture
	$(MAKE) test-lld-link LLD_LINK_TOOL="$(REPO_ROOT)/build/aux/clang/wasm-ld-no-lto.wasm" \
		LLD_LINK_COMMANDS="$(LLD_LINK_DIR)/sdk-commands.sh"
