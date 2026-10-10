# Source-built guest Vim; mirrors the Bash upstream-submodule pattern.
VIM_DIR := $(AUX_DIR)/vim
VIM_BUILD := $(BUILD_DIR)/vim
VIM_SOURCE := $(VIM_BUILD)/source
# Vim's configure wrapper and Makefile only support in-tree builds (auto/configure
# and auto/config.cache paths are hardcoded relative to src/), so we configure and
# compile inside source/src/.
VIM_CONFIGURE := $(VIM_SOURCE)/src
VIM_UPSTREAM := $(REPO_ROOT)/submodules/vim
VIM_REVISION := $(shell git -C "$(VIM_UPSTREAM)" rev-parse HEAD 2>/dev/null || cat "$(VIM_UPSTREAM)/.waste-source-revision" 2>/dev/null || echo unknown)
VIM_STAGE := $(VIM_SOURCE)/.prepared-$(VIM_REVISION)
VIM_CONFIGURED := $(VIM_CONFIGURE)/.configured
VIM_COMPILED := $(VIM_BUILD)/.compiled
VIM_INPUTS := $(shell find "$(VIM_UPSTREAM)" -type f ! -name .git 2>/dev/null)
$(VIM_INPUTS): ;
VIM_PRIVATE_HEADERS := $(shell find "$(VIM_DIR)/include" -type f -name '*.h' 2>/dev/null)
VIM_STUB_LIBS := $(VIM_BUILD)/stub-libs
VIM_RUNTIME_DIR := /usr/share/vim
VIM_STUB_ARCHIVES := $(VIM_STUB_LIBS)/libncurses.a $(VIM_STUB_LIBS)/libtinfo.a \
	$(VIM_STUB_LIBS)/libtermcap.a $(VIM_STUB_LIBS)/libtermlib.a $(VIM_STUB_LIBS)/libcurses.a
VIM_ENV = WASTE_SDK_ROOT="$(SDK_ROOT)" WASTE_VIM_CC="$(CC)" WASTE_VIM_STUB_LIBS="$(VIM_STUB_LIBS)"
VIM_LDFLAGS := -Wl,--no-entry,--import-memory,--import-table,--export=_start \
	-Wl,--export=main,--export=__heap_base,--export=__data_end,--export=__stack_pointer \
	-Wl,--experimental-pic,--unresolved-symbols=import-dynamic,--gc-sections \
	-Wl,--global-base=655360,--table-base=1024,-z,stack-size=524288

.PHONY: vim install-vim
all: vim
vim: $(VIM_BUILD)/vim.wasm

$(VIM_STAGE): $(VIM_INPUTS) $(VIM_DIR)/stage.sh $(VIM_DIR)/vim-waste.patch
	bash "$(VIM_DIR)/stage.sh" "$(abspath $(VIM_BUILD))"
	touch "$@"

$(VIM_BUILD)/libc-config.site: $(LIBC_REVIEW) $(SDK_ROOT)/lib/libc.so.wasm
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(SDK_ROOT)/lib/libc.so.wasm" --configure-cache "$@"

$(VIM_STUB_ARCHIVES): $(VIM_DIR)/vim.mk
	mkdir -p "$(@D)"
	: > "$(@D)/empty-stub.c"
	bash "$(VIM_DIR)/cc.sh" -c "$(@D)/empty-stub.c" -o "$(@D)/empty-stub.o"
	$(AR) rcs "$@" "$(@D)/empty-stub.o"

$(VIM_CONFIGURED): $(VIM_DIR)/vim.mk $(VIM_STAGE) $(VIM_DIR)/cc.sh $(VIM_DIR)/config.site $(VIM_PRIVATE_HEADERS) $(SDK_HEADERS) $(VIM_BUILD)/libc-config.site $(VIM_STUB_ARCHIVES)
	@case "$(abspath $(VIM_CONFIGURE))" in \
		"$(REPO_ROOT)/build/"*) ;; \
		*) echo 'error: Vim outputs must stay under build/' >&2; exit 1 ;; \
	esac
	cd "$(VIM_CONFIGURE)" && $(VIM_ENV) \
		CONFIG_SITE="$(abspath $(VIM_BUILD))/libc-config.site $(VIM_DIR)/config.site" \
		CC="bash $(VIM_DIR)/cc.sh" CC_FOR_BUILD="$(HOST_CC)" CFLAGS_FOR_BUILD="-std=gnu11 -O2" \
		AR="$(AR)" RANLIB="$(RANLIB)" CFLAGS="$(CFLAGS) -DNDEBUG" \
		PKG_CONFIG=/bin/false \
		./configure --host=wasm32-unknown-none --prefix=/usr \
		--with-features=tiny --with-tlib=ncurses \
		--disable-gui --disable-netbeans --disable-channel --disable-xim \
		--disable-smack --disable-selinux --disable-acl --disable-nls \
		--disable-rightleft --disable-arabic --disable-farsi --disable-hangulinput \
		--disable-xsmp --disable-sysmouse --disable-gpm --disable-mouse \
		--without-x --with-vim-name=vim \
		vim_cv_toupper_broken=no vim_cv_terminfo=no vim_cv_tgetent=zero \
		vim_cv_getcwd_broken=no vim_cv_stat_ignores_slash=no \
		vim_cv_memmove_handles_overlap=yes \
		vim_cv_timer_create=no vim_cv_timer_create_with_lrt=no vim_cv_timer_create_works=no \
		vim_cv_ipv4_networking=no vim_cv_ipv6_networking=no \
		vim_cv_uname_output=Linux vim_cv_uname_r_output=6.0.0 vim_cv_uname_m_output=wasm32 \
		> configure.log 2>&1
	touch "$@"

$(VIM_BUILD)/waste-crt.o: $(CRT_SOURCE) $(VIM_DIR)/vim.mk
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) -DWASTE_MAIN_TWO_ARGS -c "$<" -o "$@"

$(VIM_COMPILED): $(VIM_CONFIGURED) $(VIM_BUILD)/waste-crt.o $(VIM_DIR)/vim.mk
	$(VIM_ENV) $(MAKE) -C "$(VIM_CONFIGURE)" vim \
		LDFLAGS="$(VIM_LDFLAGS) $(abspath $(VIM_BUILD))/waste-crt.o" \
		VIMRUNTIMEDIR="$(VIM_RUNTIME_DIR)"
	touch "$@"

$(VIM_BUILD)/vim.wasm: $(VIM_COMPILED) $(LIBC_REVIEW) $(SDK_ROOT)/lib/libc.so.wasm
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(SDK_ROOT)/lib/libc.so.wasm" \
		--rewrite "$(VIM_CONFIGURE)/vim" --output "$@"

install-vim: vim $(VIM_DIR)/defaults.vim
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(VFS_ROOT)/lib/libc.so.wasm" \
		--check-imports "$(VIM_BUILD)/vim.wasm"
	$(PYTHON) "$(LIBC_REVIEW)" --namespace libncurses \
		--provider "$(VFS_ROOT)/lib/libncurses.so.wasm" --check-imports "$(VIM_BUILD)/vim.wasm"
	flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(VFS_TOOL)" install \
		--root "$(VFS_ROOT)" --component vim --source "$(VIM_BUILD)/vim.wasm"
	flock "$(BUILD_DIR)/.vfs-install.lock" bash -c 'mkdir -p "$$1/usr/share/licenses/vim" && install -m 644 "$$2/LICENSE" "$$1/usr/share/licenses/vim/LICENSE"' -- "$(VFS_ROOT)" "$(VIM_SOURCE)"
	flock "$(BUILD_DIR)/.vfs-install.lock" bash -c 'mkdir -p "$$1$$2" && install -m 644 "$$3" "$$1$$2/defaults.vim"' -- "$(VFS_ROOT)" "$(VIM_RUNTIME_DIR)" "$(VIM_DIR)/defaults.vim"

.PHONY: test-vim
test-vim: install-vim aux-test-runner
	bash "$(AUX_DIR)/run-wast-tests.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" vim \
		"$(VIM_DIR)/tests" "$(abspath $(VIM_BUILD)/tests)" "$(abspath $(WASM_RUNNER))" \
		"$(VIM_DIR)/tests/setup.sh"
