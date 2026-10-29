# GNU M4's master checkout is a redirect; read a pinned stable release tree.
M4_DIR := $(AUX_DIR)/m4
M4_BUILD := $(BUILD_DIR)/m4
M4_SOURCE := $(M4_BUILD)/source
M4_CONFIGURE := $(M4_BUILD)/configure
M4_REVISION := $(shell cat "$(M4_DIR)/source-revision")
M4_GNULIB_REVISION := $(shell cat "$(M4_DIR)/gnulib-revision")
M4_STAGE := $(M4_SOURCE)/.prepared-$(M4_REVISION)-$(M4_GNULIB_REVISION)
M4_CONFIGURED := $(M4_CONFIGURE)/.configured
M4_PRIVATE_HEADERS := $(REPO_ROOT)/src/config.h $(M4_DIR)/compat.h $(M4_DIR)/waste-process.h $(shell find "$(AUX_DIR)/coreutils/include" -type f -name '*.h')
M4_ENV = WASTE_SDK_ROOT="$(SDK_ROOT)" WASTE_M4_CC="$(CC)"
M4_LDFLAGS := -Wl,--no-entry,--import-memory,--import-table,--export=_start \
	-Wl,--export=main,--export=__heap_base,--export=__data_end,--export=__stack_pointer \
	-Wl,--experimental-pic,--unresolved-symbols=import-dynamic,--gc-sections \
	-Wl,--global-base=655360,--table-base=1024,-z,stack-size=524288

.PHONY: m4 install-m4 test-m4 m4-source-package
all: m4
m4: $(M4_BUILD)/m4.wasm

$(M4_STAGE): $(M4_DIR)/source-revision $(M4_DIR)/gnulib-revision $(M4_DIR)/stage.sh $(M4_DIR)/archive-source.sh $(M4_DIR)/bootstrap.sh $(wildcard $(M4_DIR)/*.patch)
	mkdir -p "$(BUILD_DIR)"
	bash "$(M4_DIR)/stage.sh" "$(abspath $(M4_BUILD))" > "$(abspath $(BUILD_DIR))/m4-bootstrap.log" 2>&1
	touch "$@"

$(M4_BUILD)/libc-config.site: $(LIBC_REVIEW) $(SDK_ROOT)/lib/libc.so.wasm
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(SDK_ROOT)/lib/libc.so.wasm" --configure-cache "$@"

$(M4_CONFIGURED): $(M4_STAGE) $(M4_DIR)/m4.mk $(M4_DIR)/cc.sh $(M4_DIR)/config.site $(M4_PRIVATE_HEADERS) $(SDK_HEADERS) $(M4_BUILD)/libc-config.site
	mkdir -p "$(M4_CONFIGURE)"
	cd "$(M4_CONFIGURE)" && $(M4_ENV) \
		CONFIG_SITE="$(abspath $(M4_BUILD))/libc-config.site $(M4_DIR)/config.site" \
		CC="bash $(M4_DIR)/cc.sh" AR="$(AR)" RANLIB="$(RANLIB)" CFLAGS="$(CFLAGS) -DNDEBUG" \
		"$(abspath $(M4_SOURCE))/configure" --host=wasm32-unknown-none --prefix=/usr \
		--disable-nls --disable-gcc-warnings --disable-year2038 --without-libsigsegv \
		--with-syscmd-shell=/usr/bin/bash > configure.log 2>&1
	$(MAKE) -C "$(M4_CONFIGURE)/lib" clean
	$(MAKE) -C "$(M4_CONFIGURE)/src" clean
	touch "$@"

$(M4_BUILD)/waste-crt.o: $(CRT_SOURCE) $(M4_DIR)/m4.mk $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) -DWASTE_MAIN_TWO_ARGS -c "$<" -o "$@"

$(M4_BUILD)/process.o: $(M4_DIR)/process.c $(M4_DIR)/waste-process.h $(M4_DIR)/m4.mk $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) -Wall -Wextra -Werror -c "$<" -o "$@"

$(M4_BUILD)/.compiled: $(M4_BUILD)/process.o $(M4_CONFIGURED) $(M4_BUILD)/waste-crt.o $(M4_DIR)/m4.mk
	# Relink when the external CRT changes even if upstream objects are cached.
	rm -f "$(M4_CONFIGURE)/src/m4"
	$(M4_ENV) $(MAKE) -C "$(M4_CONFIGURE)/lib" all V=1
	$(M4_ENV) $(MAKE) -C "$(M4_CONFIGURE)/src" m4 V=1 \
		LDFLAGS="$(M4_LDFLAGS)" LIBS="$(abspath $(M4_BUILD))/waste-crt.o $(abspath $(M4_BUILD))/process.o"
	touch "$@"

$(M4_BUILD)/m4.wasm: $(M4_BUILD)/.compiled $(LIBC_REVIEW) $(SDK_ROOT)/lib/libc.so.wasm
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(SDK_ROOT)/lib/libc.so.wasm" \
		--rewrite "$(M4_CONFIGURE)/src/m4" --output "$@"

install-m4: m4
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$(M4_BUILD)/m4.wasm"
	flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(VFS_TOOL)" install \
		--root "$(VFS_ROOT)" --component m4 --source "$(M4_BUILD)/m4.wasm"
	flock "$(BUILD_DIR)/.vfs-install.lock" bash -c 'mkdir -p "$$1/usr/share/licenses/m4" && install -m 644 "$$2/COPYING" "$$1/usr/share/licenses/m4/COPYING"' -- "$(VFS_ROOT)" "$(M4_SOURCE)"

test-m4: install-m4 aux-test-runner
	WASTE_TEST_TIMEOUT_MS="$${WASTE_TEST_TIMEOUT_MS:-300000}" bash "$(AUX_DIR)/run-wast-tests.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" m4 \
		"$(M4_DIR)/test/system" "$(abspath $(M4_BUILD)/tests)" "$(abspath $(WASM_RUNNER))" \
		"$(M4_DIR)/test/system/setup.sh"

m4-source-package: m4 $(M4_DIR)/source-package.sh $(M4_DIR)/archive-source.sh
	bash "$(M4_DIR)/source-package.sh" "$(REPO_ROOT)" "$(abspath $(M4_BUILD))"
