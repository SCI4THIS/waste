# OpenSSL source stays read-only; configuration and all products live in build/.
OPENSSL_DIR := $(AUX_DIR)/openssl
OPENSSL_BUILD := $(BUILD_DIR)/openssl
OPENSSL_SOURCE := $(OPENSSL_BUILD)/source
OPENSSL_CONFIGURE := $(OPENSSL_BUILD)/configure
OPENSSL_UPSTREAM := $(REPO_ROOT)/submodules/openssl
OPENSSL_REVISION := $(shell git -C "$(OPENSSL_UPSTREAM)" rev-parse HEAD)
OPENSSL_INPUTS := $(shell find "$(OPENSSL_UPSTREAM)" -type f ! -name .git)
OPENSSL_STAGE := $(OPENSSL_SOURCE)/.prepared-$(OPENSSL_REVISION)
OPENSSL_CONFIGURED := $(OPENSSL_CONFIGURE)/.configured
OPENSSL_COMPILED := $(OPENSSL_BUILD)/.compiled
OPENSSL_ENV = WASTE_SDK_ROOT="$(SDK_ROOT)" WASTE_OPENSSL_CC="$(CC)"
OPENSSL_OPTIONS := waste-wasm32 no-shared no-tests no-asm no-threads no-async \
	no-dso no-module no-engine no-secure-memory no-quic no-sock \
	--with-rand-seed=getrandom --prefix=/usr --openssldir=/etc/ssl
OPENSSL_CRT := $(OPENSSL_BUILD)/waste-crt.o
OPENSSL_STDIO := $(OPENSSL_BUILD)/shared-stdio.o

.PHONY: openssl libcrypto install-openssl install-libcrypto test-openssl test-libcrypto
all: openssl libcrypto
openssl: $(OPENSSL_BUILD)/openssl.wasm
libcrypto: $(OPENSSL_BUILD)/libcrypto.so.wasm

$(OPENSSL_STAGE): $(OPENSSL_INPUTS) $(OPENSSL_DIR)/stage.sh $(OPENSSL_DIR)/waste.conf $(OPENSSL_DIR)/waste.patch
	bash "$(OPENSSL_DIR)/stage.sh" "$(abspath $(OPENSSL_BUILD))"
	touch "$@"

$(OPENSSL_CONFIGURED): $(OPENSSL_STAGE) $(OPENSSL_DIR)/openssl.mk $(OPENSSL_DIR)/cc.sh $(filter-out $(SDK_ROOT)/usr/include/openssl/%,$(SDK_HEADERS))
	mkdir -p "$(OPENSSL_CONFIGURE)"
	cd "$(OPENSSL_CONFIGURE)" && $(OPENSSL_ENV) \
		CC="bash $(OPENSSL_DIR)/cc.sh" AR=llvm-ar RANLIB=llvm-ranlib \
		perl "$(abspath $(OPENSSL_SOURCE))/Configure" $(OPENSSL_OPTIONS) > configure.log 2>&1
	touch "$@"

# Upstream owns source selection and generated headers, but never links a host
# application: the final Wasm links below use the shared guest providers.
$(OPENSSL_COMPILED): $(OPENSSL_CONFIGURED)
	$(OPENSSL_ENV) $(MAKE) -C "$(OPENSSL_CONFIGURE)" build_generated libcrypto.a libssl.a apps/libapps.a
	$(OPENSSL_ENV) $(MAKE) -C "$(OPENSSL_CONFIGURE)" apps/openssl LDFLAGS="-Wl,--no-entry,--allow-undefined,--import-memory,--import-table,--export=main"
	touch "$@"

$(OPENSSL_CRT): $(CRT_SOURCE) $(OPENSSL_DIR)/openssl.mk
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) -DWASTE_MAIN_TWO_ARGS -c "$<" -o "$@"

$(OPENSSL_STDIO): $(AUX_DIR)/libncurses/shared-stdio.c $(filter-out $(SDK_ROOT)/usr/include/openssl/%,$(SDK_HEADERS))
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) -c "$<" -o "$@"

$(OPENSSL_BUILD)/libcrypto.so.wasm: $(OPENSSL_COMPILED) $(OPENSSL_STDIO) $(LIBC_REVIEW) $(SDK_ROOT)/lib/libc.so.wasm
	$(WASM_LD) --shared --import-memory --import-table --export-all --allow-undefined --no-entry \
		-o "$(OPENSSL_BUILD)/libcrypto-raw.wasm" --whole-archive "$(OPENSSL_CONFIGURE)/libcrypto.a" \
		--no-whole-archive "$(OPENSSL_STDIO)"
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(SDK_ROOT)/lib/libc.so.wasm" \
		--rewrite "$(OPENSSL_BUILD)/libcrypto-raw.wasm" --output "$@"

$(OPENSSL_BUILD)/openssl.wasm: $(OPENSSL_BUILD)/libcrypto.so.wasm $(OPENSSL_CRT)
	$(WASM_LD) $(LDFLAGS) $(WASM_LDFLAGS) --global-base=2097152 -z stack-size=1048576 \
		-o "$(OPENSSL_BUILD)/openssl-raw.wasm" "$(OPENSSL_CRT)" \
		$$(find "$(OPENSSL_CONFIGURE)/apps" -name 'openssl-bin-*.o' -type f | sort) \
		"$(OPENSSL_CONFIGURE)/apps/libapps.a" "$(OPENSSL_CONFIGURE)/libssl.a"
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(OPENSSL_BUILD)/libcrypto.so.wasm" --namespace libcrypto \
		--rewrite "$(OPENSSL_BUILD)/openssl-raw.wasm" --output "$@"
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(SDK_ROOT)/lib/libc.so.wasm" --rewrite "$@"

install-libcrypto: libcrypto $(OPENSSL_DIR)/install.py $(OPENSSL_DIR)/openssl.cnf
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$(OPENSSL_BUILD)/libcrypto.so.wasm"
	flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(OPENSSL_DIR)/install.py" \
		--root "$(VFS_ROOT)" --source "$(OPENSSL_SOURCE)" --build "$(OPENSSL_CONFIGURE)"

install-openssl: openssl install-libcrypto
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$(OPENSSL_BUILD)/openssl.wasm"
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(VFS_ROOT)/lib/libcrypto.so.wasm" --namespace libcrypto --check-imports "$(OPENSSL_BUILD)/openssl.wasm"
	flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(VFS_TOOL)" install \
		--root "$(VFS_ROOT)" --component openssl --source "$(OPENSSL_BUILD)/openssl.wasm"

# Tests import the installed providers and execute commands through guest Bash.
test-libcrypto test-openssl: install-openssl aux-test-runner
	WASTE_TEST_TIMEOUT_MS="$${WASTE_TEST_TIMEOUT_MS:-300000}" bash "$(AUX_DIR)/run-wast-tests.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" openssl \
		"$(OPENSSL_DIR)/test/system" "$(abspath $(OPENSSL_BUILD))/tests" "$(abspath $(WASM_RUNNER))" \
		"$(OPENSSL_DIR)/test/system/setup.sh"
