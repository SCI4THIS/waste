# Application-only opt-in: fixture CRTs and shared libraries remain unchanged.
NOTICE_DIR := $(AUX_DIR)/notices
NOTICE_HEADER := $(NOTICE_DIR)/options.h
NOTICE_INSTALL := $(NOTICE_DIR)/install.py
NOTICE_LOCAL := clipboard upload download ldd rogue

$(addsuffix /notice-crt.o,$(addprefix $(BUILD_DIR)/,$(NOTICE_LOCAL))): $(BUILD_DIR)/%/notice-crt.o: $(CRT_SOURCE) $(NOTICE_HEADER) $(NOTICE_DIR)/notices.mk $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) -DWASTE_SHARED_LIBC_IMPORTS \
		-DWASTE_NOTICE_PACKAGE='"$*"' -DWASTE_NOTICE_LICENSE='"$(if $(filter rogue,$*),LICENSE.TXT,LICENSE)"' \
		-Wall -Wextra -Werror -c "$<" -o "$@"

# Each Coreutils command gets its own authors extract; all share COPYING.
$(COREUTILS_BUILD)/crt-%.o: $(CRT_SOURCE) $(NOTICE_HEADER) $(NOTICE_DIR)/notices.mk $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) -DWASTE_MAIN_TWO_ARGS \
		-DWASTE_NOTICE_PACKAGE='"coreutils/$*"' \
		-DWASTE_NOTICE_LICENSE_PATH='"/usr/share/licenses/coreutils/COPYING"' \
		$(if $(filter false,$*),-DWASTE_NOTICE_FALSE) $(if $(filter echo,$*),-DWASTE_NOTICE_ECHO) \
		-Wall -Wextra -Werror -c "$<" -o "$@"

define NOTICE_CRT
$(1): CPPFLAGS += -DWASTE_NOTICE_PACKAGE='"$(2)"' -DWASTE_NOTICE_LICENSE='"$(3)"'
$(1): $(NOTICE_HEADER) $(NOTICE_DIR)/notices.mk $(SDK_HEADERS)
endef
$(eval $(call NOTICE_CRT,$(BASH_BUILD)/waste-crt.o,bash,COPYING))
$(eval $(call NOTICE_CRT,$(AWK_BUILD)/waste-crt.o,awk,COPYING))
$(eval $(call NOTICE_CRT,$(M4_BUILD)/waste-crt.o,m4,COPYING))
$(eval $(call NOTICE_CRT,$(DOT_BUILD)/waste-crt.o,dot,COPYING))
$(eval $(call NOTICE_CRT,$(VIM_BUILD)/waste-crt.o,vim,LICENSE))
$(eval $(call NOTICE_CRT,$(OPENSSL_CRT),openssl,LICENSE.txt))
# Clang and both LLD frontends use the same CRT and LLVM notice resources.
$(eval $(call NOTICE_CRT,$(CLANG_BUILD)/waste-crt.o,llvm,LICENSE.TXT))
$(CLANG_GUEST_BUILD)/.configured $(COFF_LLD_GUEST)/.configured: $(NOTICE_DIR)/link-depends.cmake

# Notice publication is explicit and uses the same lock as binary publication.
# Upstream files are read from the exact staged source used for the command.
define NOTICE_INSTALL_RULE
.PHONY: install-notices-$(1)
install-$(1): install-notices-$(1)
install-notices-$(1): $(NOTICE_INSTALL) $(3)
	mkdir -p "$(BUILD_DIR)"
	flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(NOTICE_INSTALL)" \
		--root "$(VFS_ROOT)" --package "$(1)" $(if $(2),--source "$(2)") $(4)
endef
$(foreach utility,clipboard upload download ldd,$(eval $(call NOTICE_INSTALL_RULE,$(utility),,$(AUX_DIR)/$(utility)/AUTHORS $(REPO_ROOT)/LICENSE)))
$(eval $(call NOTICE_INSTALL_RULE,rogue,$(ROGUE_UPSTREAM),$(ROGUE_UPSTREAM)/LICENSE.TXT))
$(eval $(call NOTICE_INSTALL_RULE,bash,$(BASH_SOURCE),$(BASH_STAGE)))
$(eval $(call NOTICE_INSTALL_RULE,awk,$(AWK_SOURCE),$(AWK_STAGE)))
$(eval $(call NOTICE_INSTALL_RULE,m4,$(M4_SOURCE),$(M4_STAGE)))
$(eval $(call NOTICE_INSTALL_RULE,dot,$(DOT_SOURCE),$(DOT_STAGE)))
$(eval $(call NOTICE_INSTALL_RULE,vim,$(VIM_SOURCE),$(VIM_STAGE)))
$(eval $(call NOTICE_INSTALL_RULE,openssl,$(OPENSSL_SOURCE),$(OPENSSL_STAGE)))
$(eval $(call NOTICE_INSTALL_RULE,llvm,$(CLANG_SOURCE),$(CLANG_STAGE)))
$(eval $(call NOTICE_INSTALL_RULE,coreutils,$(COREUTILS_SOURCE),$(COREUTILS_STAGE_STAMP),--commands $(COREUTILS_UTILITIES)))
$(addprefix install-,$(COREUTILS_UTILITIES)): install-notices-coreutils
install-clang install-lld-link: install-notices-llvm

.PHONY: test-notices
test-notices: aux-test-runner
	bash "$(AUX_DIR)/run-wast-tests.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" notices \
		"$(VFS_ROOT)/root/test/system/notices" "$(abspath $(BUILD_DIR)/notices/tests)" \
		"$(abspath $(WASM_RUNNER))" "$(VFS_ROOT)/root/test/system/notices/setup.sh"
