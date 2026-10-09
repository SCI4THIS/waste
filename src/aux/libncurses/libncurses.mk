# Shared-library package; included by src/aux/Makefile.
NCURSES_DIR := $(AUX_DIR)/libncurses
NCURSES_BUILD := $(BUILD_DIR)/libncurses
NCURSES_SOURCE := $(NCURSES_BUILD)/source
NCURSES_CONFIGURE := $(NCURSES_BUILD)/build
NCURSES_UPSTREAM := $(REPO_ROOT)/submodules/ncurses
NCURSES_REVISION := $(shell git -C "$(NCURSES_UPSTREAM)" rev-parse HEAD)
NCURSES_INPUTS := $(shell find "$(NCURSES_UPSTREAM)" -type f ! -name .git)
NCURSES_STAGE := $(NCURSES_SOURCE)/.prepared-$(NCURSES_REVISION)
NCURSES_CONFIGURED := $(NCURSES_CONFIGURE)/.configured
NCURSES_GENERATED := $(NCURSES_CONFIGURE)/.generated
NCURSES_ENV = WASTE_SDK_ROOT="$(SDK_ROOT)" WASTE_NCURSES_CC="$(CC)"
NCURSES_CFLAGS := --target=wasm32 -std=c11 -ffreestanding -fno-builtin \
	-DWASTE_WASM -fno-stack-protector -fdata-sections -ffunction-sections \
	-nostdinc -nostdlib -fPIC -fvisibility=default -include stdbool.h \
	-DHAVE_CONFIG_H -DNDEBUG -DBUILDING_NCURSES \
	-I"$(NCURSES_CONFIGURE)/include" -I"$(NCURSES_CONFIGURE)/ncurses" \
	-I"$(NCURSES_SOURCE)/include" -I"$(NCURSES_SOURCE)/ncurses" \
	-I"$(NCURSES_SOURCE)/ncurses/tinfo" \
	-isystem "$(SDK_ROOT)/usr/include" -isystem "$(SDK_ROOT)/usr/lib/waste/cc/include" \
	-Wno-unused-parameter -Wno-sign-compare \
	-Wno-implicit-function-declaration -Wno-int-conversion
NCURSES_LDFLAGS := --shared --import-memory --import-table --export-all --allow-undefined --no-entry
NCURSES_OPTIONS := --host=wasm32-unknown-none --prefix=/usr --without-shared \
	--without-cxx --without-cxx-binding --without-ada --without-manpages \
	--without-progs --without-tack --without-tests --without-dlsym \
	--disable-database --enable-termcap --with-fallbacks=xterm,xterm-256color,vt100,dumb \
	--with-default-terminfo-dir=/usr/share/terminfo --disable-db-install \
	--disable-home-terminfo --disable-stripping --with-bool=unsigned
NCURSES_GENERATED_C := lib_gen.c lib_keyname.c names.c codes.c unctrl.c \
	comp_captab.c comp_userdefs.c fallback.c
TIC ?= tic
INFOCMP ?= infocmp
HOST_CC ?= cc
RANLIB ?= ranlib

include $(NCURSES_DIR)/sources.mk
NCURSES_OBJECTS := $(foreach group,BASE TTY TINFO TRACE,\
	$(addprefix $(NCURSES_BUILD)/objects/$(shell printf '%s' '$(group)' | tr A-Z a-z)_,\
	$(NCURSES_$(group)_SOURCES:.c=.o))) \
	$(addprefix $(NCURSES_BUILD)/objects/generated_,$(NCURSES_GENERATED_C:.c=.o)) \
	$(NCURSES_BUILD)/objects/shared-stdio.o

.PHONY: libncurses install-libncurses
all: libncurses
libncurses: $(NCURSES_BUILD)/libncurses.so.wasm

$(NCURSES_STAGE): $(NCURSES_INPUTS) $(NCURSES_DIR)/stage.sh
	bash "$(NCURSES_DIR)/stage.sh" "$(abspath $(NCURSES_BUILD))"
	touch "$@"

$(NCURSES_CONFIGURED): $(NCURSES_STAGE) $(NCURSES_DIR)/config.site $(NCURSES_DIR)/cc.sh $(AUX_DIR)/Makefile $(SDK_HEADERS)
	@case "$(abspath $(NCURSES_CONFIGURE))" in \
		"$(REPO_ROOT)/build/"*) rm -rf -- "$(NCURSES_CONFIGURE)" ;; \
		*) echo 'error: ncurses outputs must stay under build/' >&2; exit 1 ;; \
	esac
	mkdir -p "$(NCURSES_CONFIGURE)"
	cd "$(NCURSES_CONFIGURE)" && $(NCURSES_ENV) \
		CONFIG_SITE="$(NCURSES_DIR)/config.site" CC="bash $(NCURSES_DIR)/cc.sh" \
		CPP="bash $(NCURSES_DIR)/cc.sh -E" AR="$(AR)" RANLIB="$(RANLIB)" \
		LD="$(WASM_LD)" CFLAGS="$(CFLAGS) -DNDEBUG" LDFLAGS="" \
		"$(abspath $(NCURSES_SOURCE))/configure" $(NCURSES_OPTIONS) > configure.log 2>&1
	touch "$@"

# One upstream Make invocation owns the generators; no guest binaries are built.
$(NCURSES_GENERATED): $(NCURSES_CONFIGURED) $(NCURSES_DIR)/libncurses.mk $(NCURSES_DIR)/sources.mk $(NCURSES_DIR)/fallbacks.sh
	$(NCURSES_ENV) $(MAKE) -C "$(NCURSES_CONFIGURE)/include"
	$(NCURSES_ENV) $(MAKE) -C "$(NCURSES_CONFIGURE)/ncurses" BUILD_CC="$(HOST_CC)" \
		lib_gen.c lib_keyname.c names.c codes.c unctrl.c init_keytry.h keys.list \
		comp_captab.c comp_userdefs.c
	bash "$(NCURSES_DIR)/fallbacks.sh" "$(abspath $(NCURSES_SOURCE))" \
		"$(abspath $(NCURSES_CONFIGURE))" "$(TIC)" "$(INFOCMP)"
	touch "$@"

# Compiler dependency files name these generated C inputs directly. Wait for
# their owner before checking them, including when reconfigure removes them.
$(addprefix $(NCURSES_CONFIGURE)/ncurses/,$(NCURSES_GENERATED_C)): | $(NCURSES_GENERATED)
	@test -f "$@"

define NCURSES_GROUP
$(addprefix $(NCURSES_SOURCE)/ncurses/$(1)/,$(NCURSES_$(2)_SOURCES)): | $(NCURSES_STAGE)
	@test -f "$$@"

$(addprefix $(NCURSES_BUILD)/objects/$(1)_,$(NCURSES_$(2)_SOURCES:.c=.o)): $(NCURSES_BUILD)/objects/$(1)_%.o: $(NCURSES_GENERATED) $(SDK_HEADERS)
	mkdir -p "$$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(NCURSES_CFLAGS) -MMD -MP \
		-c "$(NCURSES_SOURCE)/ncurses/$(1)/$$*.c" -o "$$@"
endef
$(eval $(call NCURSES_GROUP,base,BASE))
$(eval $(call NCURSES_GROUP,tty,TTY))
$(eval $(call NCURSES_GROUP,tinfo,TINFO))
$(eval $(call NCURSES_GROUP,trace,TRACE))

$(addprefix $(NCURSES_BUILD)/objects/generated_,$(NCURSES_GENERATED_C:.c=.o)): $(NCURSES_BUILD)/objects/generated_%.o: $(NCURSES_GENERATED) $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(NCURSES_CFLAGS) -MMD -MP \
		-c "$(NCURSES_CONFIGURE)/ncurses/$*.c" -o "$@"

$(NCURSES_BUILD)/objects/shared-stdio.o: $(NCURSES_DIR)/shared-stdio.c $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) -MMD -MP -c "$<" -o "$@"

$(NCURSES_BUILD)/libncurses.so.wasm: $(NCURSES_OBJECTS) $(NCURSES_DIR)/libncurses.mk $(LIBC_REVIEW) $(SDK_ROOT)/lib/libc.so.wasm
	$(WASM_LD) $(LDFLAGS) $(NCURSES_LDFLAGS) -o "$(NCURSES_BUILD)/libncurses-raw.wasm" $(foreach object,$(NCURSES_OBJECTS),"$(object)")
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(SDK_ROOT)/lib/libc.so.wasm" \
		--rewrite "$(NCURSES_BUILD)/libncurses-raw.wasm" --output "$@"

install-libncurses: libncurses
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(VFS_ROOT)/lib/libc.so.wasm" \
		--check-imports "$(NCURSES_BUILD)/libncurses.so.wasm"
	flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(REPO_ROOT)/src/html-rt/tools/build-guest-sdk.py" \
		--install --root "$(VFS_ROOT)" --library "$(abspath $(NCURSES_BUILD))/libncurses.so.wasm" \
		--ncurses-build "$(abspath $(NCURSES_CONFIGURE))"

-include $(NCURSES_OBJECTS:.o=.d)
