# Shared upstream Coreutils build, included by src/aux/Makefile.
COREUTILS_UTILITIES := basename cat chmod date dirname echo false ls printf pwd true wc
COREUTILS_DIR := $(AUX_DIR)/coreutils
COREUTILS_BUILD ?= $(BUILD_DIR)/coreutils
COREUTILS_SOURCE := $(COREUTILS_BUILD)/source
COREUTILS_CONFIGURE := $(COREUTILS_BUILD)/configure
COREUTILS_UPSTREAM := $(REPO_ROOT)/submodules/coreutils
COREUTILS_REVISION := $(shell git -C "$(COREUTILS_UPSTREAM)" rev-parse HEAD)
# Checkouts are read-only, so revision/checkout changes drive source staging.
# Avoid walking thousands of Gnulib files on every per-command invocation.
COREUTILS_UPSTREAM_INPUTS := $(COREUTILS_UPSTREAM)/bootstrap \
	$(COREUTILS_UPSTREAM)/bootstrap.conf $(COREUTILS_UPSTREAM)/configure.ac \
	$(COREUTILS_UPSTREAM)/Makefile.am $(COREUTILS_UPSTREAM)/gnulib/gnulib-tool \
	$(shell git -C "$(COREUTILS_UPSTREAM)/gnulib" rev-parse --git-path HEAD)
COREUTILS_HEADERS := $(shell find "$(COREUTILS_DIR)/include" -type f -name '*.h')
COREUTILS_STAGE_STAMP := $(COREUTILS_SOURCE)/.prepared-$(COREUTILS_REVISION)
COREUTILS_CONFIGURE_STAMP := $(COREUTILS_CONFIGURE)/.configured
COREUTILS_OBJECT_STAMP := $(COREUTILS_BUILD)/.objects-built
COREUTILS_ARCHIVE := $(COREUTILS_BUILD)/libcoreutils-runtime.a
COREUTILS_CRT := $(COREUTILS_BUILD)/waste-crt.o
COREUTILS_GENERATED := lib/configmake.h src/version.h lib/unitypes.h \
	lib/unictype.h lib/unicase.h lib/uninorm.h lib/uniwidth.h \
	lib/malloc/scratch_buffer.gl.h lib/crc-sliceby8.h lib/fts_.h \
	$(COREUTILS_SOURCE)/src/dircolors.h
COREUTILS_STRIP_OBJECTS := libcoreutils_a-fcntl.o libcoreutils_a-open.o \
	libcoreutils_a-fchmodat.o \
	libcoreutils_a-stat.o libcoreutils_a-lstat.o libcoreutils_a-fstatat.o \
	libcoreutils_a-localeconv.o libcoreutils_a-vfzprintf.o libcoreutils_a-vzprintf.o \
	libcoreutils_a-vsnzprintf.o libcoreutils_a-vszprintf.o libcoreutils_a-vaszprintf.o \
	libcoreutils_a-aszprintf.o libcoreutils_a-vasnprintf.o libcoreutils_a-vasprintf.o
COREUTILS_LDFLAGS := --no-entry --import-memory --import-table --export=_start \
	--export=__stack_pointer --export=__heap_base --allow-undefined --gc-sections \
	--global-base=655360 --table-base=1024 --initial-memory=1048576 -z stack-size=524288
COREUTILS_ENV = WASTE_COREUTILS_BUILD_DIR="$(abspath $(COREUTILS_BUILD))" \
	WASTE_SDK_ROOT="$(SDK_ROOT)" WASTE_COREUTILS_CC="$(CC)" \
	CONFIG_SITE="$(COREUTILS_DIR)/config.site"

include $(foreach utility,$(COREUTILS_UTILITIES),$(AUX_DIR)/$(utility)/sources.mk)
COREUTILS_ALL_OBJECTS := $(sort $(foreach utility,$(COREUTILS_UTILITIES),$(COREUTILS_OBJECTS_$(utility))))

.PHONY: coreutils coreutils-stage coreutils-bootstrap coreutils-source-package install-coreutils install-coreutils-notices
coreutils: $(COREUTILS_UTILITIES)
all: coreutils
coreutils-stage: $(COREUTILS_STAGE_STAMP)
coreutils-bootstrap: $(COREUTILS_SOURCE)/configure
install-coreutils: $(addprefix install-,$(COREUTILS_UTILITIES))

$(COREUTILS_STAGE_STAMP): $(COREUTILS_UPSTREAM_INPUTS) $(COREUTILS_DIR)/coreutils-waste.patch $(COREUTILS_DIR)/stage.sh
	bash "$(COREUTILS_DIR)/stage.sh" "$(abspath $(COREUTILS_BUILD))"
	touch "$@"

$(COREUTILS_SOURCE)/configure: $(COREUTILS_STAGE_STAMP) $(COREUTILS_DIR)/bootstrap.sh
	WASTE_COREUTILS_BUILD_DIR="$(abspath $(COREUTILS_BUILD))" bash "$(COREUTILS_DIR)/bootstrap.sh" --no-install
	touch "$@"

$(COREUTILS_CONFIGURE_STAMP): $(COREUTILS_SOURCE)/configure $(COREUTILS_DIR)/config.site \
		$(COREUTILS_DIR)/cc.sh $(COREUTILS_HEADERS) $(SDK_HEADERS)
	@case "$(abspath $(COREUTILS_CONFIGURE))" in \
		"$(REPO_ROOT)/build/"*) rm -rf -- "$(COREUTILS_CONFIGURE)" ;; \
		*) echo 'error: Coreutils outputs must stay under build/' >&2; exit 1 ;; \
	esac
	mkdir -p "$(COREUTILS_CONFIGURE)"
	cd "$(COREUTILS_CONFIGURE)" && $(COREUTILS_ENV) \
		CONFIG_SITE="$(COREUTILS_DIR)/config.site" CC="$(COREUTILS_DIR)/cc.sh" \
		CPP="$(COREUTILS_DIR)/cc.sh -E" CFLAGS="-g $(CFLAGS)" \
		"$(abspath $(COREUTILS_SOURCE))/configure" --host=wasm32-unknown-none \
		--disable-nls --disable-gcc-warnings --disable-year2038 > configure.log 2>&1
	touch "$@"

# One upstream Make invocation owns shared generators, objects and archives.
# Final executables can then link concurrently without modifying that archive.
$(COREUTILS_OBJECT_STAMP): $(COREUTILS_CONFIGURE_STAMP) $(COREUTILS_DIR)/coreutils.mk \
		$(foreach utility,$(COREUTILS_UTILITIES),$(AUX_DIR)/$(utility)/sources.mk)
	$(COREUTILS_ENV) $(MAKE) -C "$(COREUTILS_CONFIGURE)" -o config.status V=1 $(COREUTILS_GENERATED)
	$(COREUTILS_ENV) $(MAKE) -C "$(COREUTILS_CONFIGURE)" -o config.status V=1 \
		$(COREUTILS_ALL_OBJECTS) lib/libcoreutils.a src/libver.a
	touch "$@"

$(COREUTILS_ARCHIVE): $(COREUTILS_OBJECT_STAMP)
	cp "$(COREUTILS_CONFIGURE)/lib/libcoreutils.a" "$@"
	$(AR) d "$@" $(COREUTILS_STRIP_OBJECTS)

$(COREUTILS_CRT): $(CRT_SOURCE) $(COREUTILS_DIR)/coreutils.mk
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) --target=wasm32 -ffreestanding -fno-builtin \
		-DWASTE_MAIN_TWO_ARGS -fno-stack-protector \
		-nostdlib -c "$<" -o "$@"

define COREUTILS_RULE
.PHONY: $(1) install-$(1)
$(1): $(BUILD_DIR)/$(1)/$(1).wasm
install-$(1): $(1)
$(BUILD_DIR)/$(1)/$(1).wasm: $(COREUTILS_ARCHIVE) $(COREUTILS_CRT) \
		$(LIBC_REVIEW) $(SDK_ROOT)/lib/libc.so.wasm
	mkdir -p "$$(@D)"
	$(WASM_LD) $(LDFLAGS) $(COREUTILS_LDFLAGS) -o "$$(@D)/$(1)-raw.wasm" \
		$(foreach object,$(COREUTILS_OBJECTS_$(1)),"$(COREUTILS_CONFIGURE)/$(object)") \
		"$(COREUTILS_CONFIGURE)/src/libver.a" "$(COREUTILS_ARCHIVE)" "$(COREUTILS_ARCHIVE)" "$(COREUTILS_CRT)"
	wasm-opt "$$(@D)/$(1)-raw.wasm" --strip-debug -o "$$(@D)/$(1)-stripped.wasm"
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(SDK_ROOT)/lib/libc.so.wasm" \
		--rewrite "$$(@D)/$(1)-stripped.wasm" --output "$$@"
endef
$(foreach utility,$(COREUTILS_UTILITIES),$(eval $(call COREUTILS_RULE,$(utility))))

$(addprefix install-,$(COREUTILS_UTILITIES)): install-coreutils-notices
	@set -eu; utility="$(patsubst install-%,%,$@)"; \
		artifact="$(BUILD_DIR)/$$utility/$$utility.wasm"; \
		$(PYTHON) "$(LIBC_REVIEW)" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$$artifact"; \
		flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(VFS_TOOL)" install \
			--root "$(VFS_ROOT)" --component "$$utility" --source "$$artifact"

install-coreutils-notices: $(COREUTILS_STAGE_STAMP)
	mkdir -p "$(BUILD_DIR)"
	flock "$(BUILD_DIR)/.vfs-install.lock" bash "$(COREUTILS_DIR)/install-notices.sh" \
		"$(VFS_ROOT)" "$(abspath $(COREUTILS_BUILD))"

coreutils-source-package: coreutils $(COREUTILS_DIR)/source-package.sh
	bash "$(COREUTILS_DIR)/source-package.sh" "$(REPO_ROOT)" "$(abspath $(COREUTILS_BUILD))" "$(abspath $(BUILD_DIR))" $(COREUTILS_UTILITIES)
