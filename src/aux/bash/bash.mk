# Source-built guest Bash and the authored installed-executable launcher.
BASH_DIR := $(AUX_DIR)/bash
BASH_BUILD := $(BUILD_DIR)/bash
BASH_SOURCE := $(BASH_BUILD)/source
BASH_CONFIGURE := $(BASH_BUILD)/configure
BASH_UPSTREAM := $(REPO_ROOT)/submodules/bash
BASH_REVISION := $(shell git -C "$(BASH_UPSTREAM)" rev-parse HEAD 2>/dev/null || cat "$(BASH_UPSTREAM)/.waste-source-revision")
BASH_STAGE := $(BASH_SOURCE)/.prepared-$(BASH_REVISION)
BASH_CONFIGURED := $(BASH_CONFIGURE)/.configured
BASH_COMPILED := $(BASH_BUILD)/.compiled
BASH_INPUTS := $(shell find "$(BASH_UPSTREAM)" -type f ! -name .git)
# Disable built-in remake rules for dependency inputs (including Texinfo).
$(BASH_INPUTS): ;
BASH_PRIVATE_HEADERS := $(shell find "$(BASH_DIR)/include" -type f -name '*.h')
BASH_ENV = WASTE_SDK_ROOT="$(SDK_ROOT)" WASTE_BASH_CC="$(CC)"
BASH_LDFLAGS := -Wl,--no-entry,--import-memory,--import-table,--export=_start \
	-Wl,--export=main,--export=__heap_base,--export=__data_end,--export=__stack_pointer \
	-Wl,--experimental-pic,--unresolved-symbols=import-dynamic,--gc-sections \
	-Wl,--global-base=655360,--table-base=1024,-z,stack-size=524288
HOST_CC ?= cc
RANLIB ?= ranlib

.PHONY: bash install-bash install-bash-launch bash-source-package
all: bash
bash: $(BASH_BUILD)/bash.wasm

$(BASH_STAGE): $(BASH_INPUTS) $(BASH_DIR)/stage.sh $(BASH_DIR)/bash-waste.patch
	bash "$(BASH_DIR)/stage.sh" "$(abspath $(BASH_BUILD))"
	touch "$@"

$(BASH_BUILD)/libc-config.site: $(LIBC_REVIEW) $(SDK_ROOT)/lib/libc.so.wasm
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(SDK_ROOT)/lib/libc.so.wasm" --configure-cache "$@"

$(BASH_CONFIGURED): $(BASH_DIR)/bash.mk $(BASH_STAGE) $(BASH_DIR)/cc.sh $(BASH_DIR)/config.site $(BASH_PRIVATE_HEADERS) $(SDK_HEADERS) $(BASH_BUILD)/libc-config.site
	@case "$(abspath $(BASH_CONFIGURE))" in \
		"$(REPO_ROOT)/build/"*) rm -rf -- "$(BASH_CONFIGURE)" ;; \
		*) echo 'error: Bash outputs must stay under build/' >&2; exit 1 ;; \
	esac
	mkdir -p "$(BASH_CONFIGURE)"
	cd "$(BASH_CONFIGURE)" && $(BASH_ENV) \
		CONFIG_SITE="$(abspath $(BASH_BUILD))/libc-config.site $(BASH_DIR)/config.site" \
		CC="bash $(BASH_DIR)/cc.sh" CC_FOR_BUILD="$(HOST_CC)" CFLAGS_FOR_BUILD="-std=gnu11 -O2" \
		AR="$(AR)" RANLIB="$(RANLIB)" CFLAGS="$(CFLAGS) -DNDEBUG" \
		"$(abspath $(BASH_SOURCE))/configure" --host=wasm32-unknown-none --prefix=/usr \
		--without-bash-malloc --disable-nls --disable-multibyte --disable-net-redirections \
		--disable-cond-regexp --disable-profiling > configure.log 2>&1
	touch "$@"

$(BASH_BUILD)/waste-crt.o: $(CRT_SOURCE) $(BASH_DIR)/bash.mk
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) -c "$<" -o "$@"

$(BASH_COMPILED): $(BASH_CONFIGURED) $(BASH_BUILD)/waste-crt.o $(BASH_DIR)/bash.mk
	$(BASH_ENV) $(MAKE) -C "$(BASH_CONFIGURE)" bash \
		LDFLAGS="$(BASH_LDFLAGS) $(abspath $(BASH_BUILD))/waste-crt.o"
	touch "$@"

$(BASH_BUILD)/bash.wasm: $(BASH_COMPILED) $(LIBC_REVIEW) $(SDK_ROOT)/lib/libc.so.wasm
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(SDK_ROOT)/lib/libc.so.wasm" \
		--rewrite "$(BASH_CONFIGURE)/bash" --output "$@"

install-bash: bash install-bash-launch
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(VFS_ROOT)/lib/libc.so.wasm" \
		--check-imports "$(BASH_BUILD)/bash.wasm"
	flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(VFS_TOOL)" install \
		--root "$(VFS_ROOT)" --component bash --source "$(BASH_BUILD)/bash.wasm"

	flock "$(BUILD_DIR)/.vfs-install.lock" bash -c 'mkdir -p "$$1/usr/share/licenses/bash" && install -m 644 "$$2/COPYING" "$$1/usr/share/licenses/bash/COPYING"' -- "$(VFS_ROOT)" "$(BASH_SOURCE)"

bash-source-package: bash
	bash "$(BASH_DIR)/source-package.sh" "$(REPO_ROOT)" "$(abspath $(BASH_BUILD))"

install-bash-launch: $(BASH_DIR)/launch.wast
	mkdir -p "$(BUILD_DIR)"
	flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(VFS_TOOL)" install \
		--root "$(VFS_ROOT)" --component launch --source "$<"
