# COFF-only guest linker. The compiler's private LLVM build supplies libraries;
# this package configures and patches its own sources, never the live LLVM tree.
COFF_LLD_DIR := $(AUX_DIR)/lld-link
COFF_LLD_BUILD := $(abspath $(BUILD_DIR)/lld-link)
COFF_LLD_GUEST := $(COFF_LLD_BUILD)/guest
COFF_LLD_SOURCE := $(COFF_LLD_BUILD)/source
COFF_LLD_DLLTOOL ?= llvm-dlltool
COFF_LLD_COMPONENTS := LLVMCodeGen LLVMCore LLVMDebugInfoDWARF LLVMDemangle \
	LLVMMC LLVMOption LLVMSupport LLVMTarget LLVMTargetParser LLVMBinaryFormat \
	LLVMBitWriter LLVMDebugInfoCodeView LLVMDebugInfoMSF LLVMDebugInfoPDB \
	LLVMObject LLVMWindowsDriver LLVMWindowsManifest

.PHONY: lld-link install-lld-link test-lld-link lld-link-imports install-lld-link-imports
all: lld-link
lld-link: $(COFF_LLD_BUILD)/lld-link.wasm lld-link-imports
lld-link-imports: $(COFF_LLD_BUILD)/kernel32.lib $(COFF_LLD_BUILD)/user32.lib

$(COFF_LLD_SOURCE)/.prepared: $(CLANG_STAGE) $(COFF_LLD_DIR)/stage.sh $(COFF_LLD_DIR)/coff-waste.patch
	bash "$(COFF_LLD_DIR)/stage.sh" "$(CLANG_SOURCE)" "$(COFF_LLD_BUILD)"
	touch "$@"

$(COFF_LLD_BUILD)/%.lib: $(COFF_LLD_DIR)/%.def $(COFF_LLD_DIR)/lld-link.mk
	mkdir -p "$(@D)"
	$(COFF_LLD_DLLTOOL) -m i386:x86-64 -d "$<" -l "$@"

$(COFF_LLD_GUEST)/.configured: $(CLANG_GUEST_BUILD)/.configured $(COFF_LLD_SOURCE)/.prepared $(COFF_LLD_DIR)/CMakeLists.txt $(COFF_LLD_DIR)/lld-link.mk
	mkdir -p "$(@D)"
	$(CLANG_ENV) WASTE_CLANG_CXX_HEADERS="$(CLANG_CXX_BUILD)/include/c++/v1" \
		"$(CLANG_CMAKE)" -S "$(COFF_LLD_DIR)" -B "$(@D)" -G "Unix Makefiles" \
		-DCMAKE_TOOLCHAIN_FILE="$(CLANG_DIR)/guest-toolchain.cmake" \
		-DCMAKE_BUILD_TYPE=MinSizeRel $(CLANG_VCS_FLAGS) \
		-DLLVM_SOURCE="$(CLANG_SOURCE)" -DLLVM_BUILD="$(CLANG_GUEST_BUILD)" \
		-DCOFF_SOURCE="$(COFF_LLD_SOURCE)" -DLLVM_TABLEGEN="$(CLANG_HOST_BUILD)/bin/llvm-tblgen" \
		-DCMAKE_EXE_LINKER_FLAGS="$(CLANG_BUILD)/waste-crt.o -L$(CLANG_CXX_BUILD)/lib -lc++ -lc++abi -Wl,--no-entry,--import-memory,--import-table,--experimental-pic,--unresolved-symbols=import-dynamic,--export=_start,--export=main,--export=__heap_base,--export=__data_end,--export=__stack_pointer $(CLANG_NO_WASM_OPT)"
	touch "$@"

$(COFF_LLD_GUEST)/.libraries: $(CLANG_GUEST_BUILD)/.configured $(COFF_LLD_DIR)/lld-link.mk
	mkdir -p "$(@D)"
	$(CLANG_ENV) "$(CLANG_CMAKE)" --build "$(CLANG_GUEST_BUILD)" \
		--parallel "$(CLANG_JOBS)" --target $(COFF_LLD_COMPONENTS)
	touch "$@"

# Let CMake track source/header/archive dependencies even on cached builds.
.PHONY: lld-link-compile
lld-link-compile: $(COFF_LLD_GUEST)/.configured $(COFF_LLD_GUEST)/.libraries
	$(CLANG_ENV) "$(CLANG_CMAKE)" --build "$(COFF_LLD_GUEST)" \
		--parallel "$(CLANG_JOBS)" --target lld-link

$(COFF_LLD_BUILD)/lld-link-stripped.raw.wasm: lld-link-compile
	@if [ ! -f "$@" ] || [ "$(COFF_LLD_GUEST)/lld-link" -nt "$@" ]; then \
		$(CLANG_OBJCOPY) --remove-section=name "$(COFF_LLD_GUEST)/lld-link" "$@"; fi

$(COFF_LLD_BUILD)/lld-link.wasm: $(COFF_LLD_BUILD)/lld-link-stripped.raw.wasm $(LIBC_REVIEW) $(VFS_ROOT)/lib/libc.so.wasm
	@if [ ! -f "$@" ] || [ "$<" -nt "$@" ] || [ "$(LIBC_REVIEW)" -nt "$@" ] || [ "$(VFS_ROOT)/lib/libc.so.wasm" -nt "$@" ]; then \
		$(PYTHON) "$(LIBC_REVIEW)" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --rewrite "$<" --output "$@" && \
		$(PYTHON) "$(LIBC_REVIEW)" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$@"; fi

install-lld-link: lld-link install-lld-link-imports
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$(COFF_LLD_BUILD)/lld-link.wasm"
	flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(VFS_TOOL)" install \
		--root "$(VFS_ROOT)" --component lld-link --source "$(COFF_LLD_BUILD)/lld-link.wasm"
	flock "$(BUILD_DIR)/.vfs-install.lock" install -m 644 "$(CLANG_SOURCE)/LICENSE.TXT" "$(VFS_ROOT)/usr/share/licenses/lld-link/LICENSE.TXT"

# Refresh target-OS link metadata without rebuilding LLVM or the guest tools.
install-lld-link-imports: lld-link-imports
	flock "$(BUILD_DIR)/.vfs-install.lock" bash -c 'set -eu; \
		mkdir -p "$$1/usr/lib/waste/windows-demo" "$$1/usr/share/licenses/lld-link"; \
		install -m 644 "$$2/kernel32.lib" "$$2/user32.lib" "$$1/usr/lib/waste/windows-demo/"; \
		install -m 644 "$$3/kernel32.def" "$$3/user32.def" "$$3/README.md" "$$1/usr/share/licenses/lld-link/"' \
		-- "$(VFS_ROOT)" "$(COFF_LLD_BUILD)" "$(COFF_LLD_DIR)"

test-lld-link: install-lld-link install-clang aux-test-runner
	bash "$(COFF_LLD_DIR)/test/system/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" lld-link \
		"$(COFF_LLD_DIR)/test/system" "$(COFF_LLD_BUILD)/tests" \
		"$(abspath $(WASM_RUNNER))" "$(COFF_LLD_DIR)/test/system/setup.sh"
