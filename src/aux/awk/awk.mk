# GNU Awk, staged from the read-only Gawk submodule and installed as awk.
AWK_DIR := $(AUX_DIR)/awk
AWK_BUILD := $(BUILD_DIR)/awk
AWK_SOURCE := $(AWK_BUILD)/source
AWK_CONFIGURE := $(AWK_BUILD)/configure
AWK_UPSTREAM := $(REPO_ROOT)/submodules/gawk
AWK_REVISION := $(shell cat "$(AWK_UPSTREAM)/.waste-source-revision" 2>/dev/null || git -C "$(AWK_UPSTREAM)" rev-parse HEAD)
AWK_INPUTS := $(shell find "$(AWK_UPSTREAM)" -type f ! -name .git)
$(AWK_INPUTS): ;
AWK_STAGE := $(AWK_SOURCE)/.prepared-$(AWK_REVISION)
AWK_CONFIGURED := $(AWK_CONFIGURE)/.configured
AWK_ENV = WASTE_SDK_ROOT="$(SDK_ROOT)" WASTE_AWK_CC="$(CC)"
AWK_LDFLAGS := -Wl,--no-entry,--import-memory,--import-table,--export=_start \
	-Wl,--export=main,--export=__heap_base,--export=__data_end,--export=__stack_pointer \
	-Wl,--experimental-pic,--unresolved-symbols=import-dynamic,--gc-sections \
	-Wl,--global-base=655360,--table-base=1024,-z,stack-size=524288

.PHONY: awk install-awk test-awk awk-source-package
all: awk
awk: $(AWK_BUILD)/awk.wasm

$(AWK_STAGE): $(AWK_INPUTS) $(AWK_DIR)/stage.sh $(AWK_DIR)/waste.patch
	bash "$(AWK_DIR)/stage.sh" "$(abspath $(AWK_BUILD))"
	touch "$@"

$(AWK_BUILD)/libc-config.site: $(LIBC_REVIEW) $(LIBC_OUTPUT)
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(LIBC_OUTPUT)" --configure-cache "$@"

$(AWK_CONFIGURED): $(AWK_STAGE) $(AWK_DIR)/awk.mk $(AWK_DIR)/cc.sh $(AWK_DIR)/config.site $(SDK_HEADERS) $(AWK_BUILD)/libc-config.site
	mkdir -p "$(AWK_CONFIGURE)"
	cd "$(AWK_CONFIGURE)" && $(AWK_ENV) \
		CONFIG_SITE="$(abspath $(AWK_BUILD))/libc-config.site $(AWK_DIR)/config.site" \
		CC="bash $(AWK_DIR)/cc.sh" AR="$(AR)" RANLIB="$(RANLIB)" CFLAGS="$(CFLAGS) -DNDEBUG" \
		"$(abspath $(AWK_SOURCE))/configure" --host=wasm32-unknown-none --prefix=/usr \
		--disable-nls --disable-extensions --disable-pma --disable-mpfr --without-readline \
		> configure.log 2>&1
	touch "$@"

$(AWK_BUILD)/waste-crt.o: $(CRT_SOURCE) $(AWK_DIR)/awk.mk $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) -DWASTE_MAIN_TWO_ARGS -c "$<" -o "$@"

$(AWK_BUILD)/.compiled: $(AWK_CONFIGURED) $(AWK_BUILD)/waste-crt.o $(AWK_DIR)/awk.mk
	$(AWK_ENV) $(MAKE) -C "$(AWK_CONFIGURE)/support" libsupport.a
	$(AWK_ENV) $(MAKE) -C "$(AWK_CONFIGURE)" gawk \
		LDFLAGS="$(AWK_LDFLAGS)" LIBS="$(abspath $(AWK_BUILD))/waste-crt.o"
	touch "$@"

$(AWK_BUILD)/awk.wasm: $(AWK_BUILD)/.compiled $(LIBC_REVIEW) $(LIBC_OUTPUT)
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(LIBC_OUTPUT)" \
		--rewrite "$(AWK_CONFIGURE)/gawk" --output "$@"

install-awk: awk install-libc
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$(AWK_BUILD)/awk.wasm"
	flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(VFS_TOOL)" install \
		--root "$(VFS_ROOT)" --component awk --source "$(AWK_BUILD)/awk.wasm"
	flock "$(BUILD_DIR)/.vfs-install.lock" bash -c 'mkdir -p "$$1/usr/share/licenses/awk" && install -m 644 "$$2/COPYING" "$$1/usr/share/licenses/awk/COPYING"' -- "$(VFS_ROOT)" "$(AWK_SOURCE)"

test-awk: install-awk aux-test-runner
	WASTE_TEST_TIMEOUT_MS="$${WASTE_TEST_TIMEOUT_MS:-300000}" bash "$(AUX_DIR)/run-wast-tests.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" awk \
		"$(AWK_DIR)/test/system" "$(abspath $(AWK_BUILD)/tests)" "$(abspath $(WASM_RUNNER))" \
		"$(AWK_DIR)/test/system/setup.sh"

awk-source-package: awk $(AWK_DIR)/source-package.sh
	bash "$(AWK_DIR)/source-package.sh" "$(REPO_ROOT)" "$(abspath $(AWK_BUILD))"
