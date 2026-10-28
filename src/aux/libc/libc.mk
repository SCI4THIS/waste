# Production shared guest libc and package WAST tests.
LIBC_DIR := $(AUX_DIR)/libc
LIBC_BUILD := $(BUILD_DIR)/libc
include $(LIBC_DIR)/sources.mk
LIBC_SOURCES := allocator.c $(LIBC_C_SOURCES)
LIBC_OBJECTS := $(addprefix $(LIBC_BUILD)/objects/,$(LIBC_SOURCES:.c=.o))
LIBC_OUTPUT := $(LIBC_BUILD)/libc.so.wasm
LIBC_CFLAGS := --target=wasm32 -std=c11 -ffreestanding -fno-builtin \
	-DWASTE_WASM -DNDEBUG -fPIC -fvisibility=default \
	-DWASTE_POSIX_IO -DWASTE_SHARED_LIBC -fno-stack-protector \
	-fdata-sections -ffunction-sections -nostdinc -nostdlib \
	-isystem "$(SDK_ROOT)/usr/include" \
	-isystem "$(SDK_ROOT)/usr/lib/waste/cc/include"
include $(LIBC_DIR)/llvm-math/llvm-math.mk

LIBC_LDFLAGS := --shared --import-memory --import-table \
	--export-all --allow-undefined --no-entry

.PHONY: libc install-libc test-libc
test-libc: install-libc aux-test-runner
	bash "$(AUX_DIR)/run-wast-tests.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" libc \
		"$(LIBC_DIR)/test/system" "$(abspath $(LIBC_BUILD))/tests" "$(abspath $(WASM_RUNNER))"
all: libc
libc: $(LIBC_OUTPUT)

$(LIBC_BUILD)/objects/%.o: $(LIBC_DIR)/%.c $(LIBC_DIR)/include/helper.h $(LIBC_DIR)/libc.mk $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIBC_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(LIBC_BUILD)/objects/stdio.o $(LIBC_BUILD)/objects/wide-format.o: $(LIBC_DIR)/include/format.h

$(LIBC_OUTPUT): $(LIBC_OBJECTS) $(LIBC_DIR)/libc.mk $(LIBC_DIR)/sources.mk
	$(WASM_LD) $(LDFLAGS) $(LIBC_LDFLAGS) -o "$@" $(foreach object,$(LIBC_OBJECTS),"$(object)")

# The VFS installer validates the shared-module format and publishes both paths.
install-libc: libc
	flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(VFS_TOOL)" install \
		--root "$(VFS_ROOT)" --component libc --source "$(LIBC_OUTPUT)"

include $(LIBC_DIR)/system.mk

-include $(LIBC_OBJECTS:.o=.d)

install-libc: install-libc-math-notices
.PHONY: install-libc-math-notices
install-libc-math-notices: $(LIBC_MATH_STAGE)
	flock "$(BUILD_DIR)/.vfs-install.lock" bash -c 'mkdir -p "$$1/usr/share/licenses/libc/llvm-math" && install -m 644 "$$2" "$$1/usr/share/licenses/libc/llvm-math/LICENSE.TXT"' -- "$(VFS_ROOT)" "$(LIBC_MATH_BUILD)/source/LICENSE.TXT"
