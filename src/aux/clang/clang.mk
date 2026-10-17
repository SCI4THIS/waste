# Source-built guest compiler. C++ runtime, host generators, and target objects
# have separate build directories; no configure/build runs in the submodule.
CLANG_DIR := $(AUX_DIR)/clang
CLANG_BUILD := $(abspath $(BUILD_DIR)/clang)
CLANG_UPSTREAM := $(REPO_ROOT)/submodules/llvm-project
CLANG_SOURCE := $(CLANG_BUILD)/source
CLANG_REVISION := $(shell git -C "$(CLANG_UPSTREAM)" rev-parse HEAD 2>/dev/null)
# Staged sources have no .git directory and must not inherit WASTE's revision.
CLANG_VCS_FLAGS := -DLLVM_FORCE_VC_REVISION="$(CLANG_REVISION)" \
	-DLLVM_FORCE_VC_REPOSITORY="https://github.com/llvm/llvm-project"
CLANG_STAGE := $(CLANG_SOURCE)/.prepared-$(CLANG_REVISION)
CLANG_CXX_BUILD := $(CLANG_BUILD)/cxx
CLANG_HOST_BUILD := $(CLANG_BUILD)/native
CLANG_GUEST_BUILD := $(CLANG_BUILD)/guest
CLANG_CMAKE ?= $(shell command -v cmake 2>/dev/null || printf '%s' "$(CLANG_BUILD)/cmake-4.1.3-linux-x86_64/bin/cmake")
CLANG_CXX ?= clang++
CLANG_HOST_CC ?= $(CC)
CLANG_AR ?= llvm-ar
CLANG_RANLIB ?= llvm-ranlib
# LLVM builds can exhaust desktop memory if Make's -j is inherited.
CLANG_JOBS ?= 1
# Some host Clang builds automatically run Binaryen after wasm-ld at -Os.
# Keep source optimization, but make whole-module optimization an explicit step
# for these large tools. Stock Clang without this option needs no extra flag.
CLANG_NO_WASM_OPT := $(findstring --no-wasm-opt,$(shell "$(CLANG_CXX)" --help 2>/dev/null))
CLANG_ENV = WASTE_SDK_ROOT="$(SDK_ROOT)" WASTE_CLANG_SOURCE="$(CLANG_SOURCE)" WASTE_CLANG_CC="$(CC)" \
	WASTE_CLANG_CXX="$(CLANG_CXX)" WASTE_CLANG_AR="$(CLANG_AR)" WASTE_CLANG_RANLIB="$(CLANG_RANLIB)"
CLANG_PROFILES := $(CLANG_DIR)/toolchain.cmake $(CLANG_DIR)/runtime.cmake $(CLANG_DIR)/llvm.cmake $(CLANG_DIR)/clang.mk

.PHONY: clang clang-source clang-cxx clang-sdk-check clang-native-tools clang-support clang-core clang-dwarf clang-gsym install-clang test-clang
all: clang
clang: $(CLANG_BUILD)/clang.wasm $(CLANG_BUILD)/wasm-ld.wasm
clang-source: $(CLANG_STAGE)
clang-cxx: $(CLANG_CXX_BUILD)/.compiled
clang-native-tools: $(CLANG_HOST_BUILD)/.compiled
clang-support: $(CLANG_GUEST_BUILD)/.configured
	$(CLANG_ENV) "$(CLANG_CMAKE)" --build "$(CLANG_GUEST_BUILD)" --parallel "$(CLANG_JOBS)" --target LLVMSupport
clang-core: $(CLANG_GUEST_BUILD)/.configured
	$(CLANG_ENV) "$(CLANG_CMAKE)" --build "$(CLANG_GUEST_BUILD)" --parallel "$(CLANG_JOBS)" --target LLVMCore
clang-dwarf: $(CLANG_GUEST_BUILD)/.configured
	$(CLANG_ENV) "$(CLANG_CMAKE)" --build "$(CLANG_GUEST_BUILD)" --parallel "$(CLANG_JOBS)" --target LLVMDebugInfoDWARF
clang-gsym: $(CLANG_GUEST_BUILD)/.configured
	$(CLANG_ENV) "$(CLANG_CMAKE)" --build "$(CLANG_GUEST_BUILD)" --parallel "$(CLANG_JOBS)" --target LLVMDebugInfoGSYM
clang-sdk-check: $(CLANG_BUILD)/probes/cxx-sdk.o

$(CLANG_STAGE): $(CLANG_DIR)/stage.sh $(wildcard $(CLANG_DIR)/clang-waste.patch) $(CLANG_UPSTREAM)/llvm/CMakeLists.txt
	bash "$(CLANG_DIR)/stage.sh" "$(CLANG_BUILD)"
	touch "$@"

$(CLANG_CXX_BUILD)/.configured: $(CLANG_STAGE) $(CLANG_PROFILES) $(SDK_HEADERS)
	mkdir -p "$(@D)"
	rm -f "$(@D)/CMakeCache.txt"
	$(CLANG_ENV) "$(CLANG_CMAKE)" -S "$(CLANG_SOURCE)/runtimes" -B "$(@D)" \
		-G "Unix Makefiles" -C "$(CLANG_DIR)/runtime.cmake" \
		-DCMAKE_TOOLCHAIN_FILE="$(CLANG_DIR)/toolchain.cmake" \
		-DCMAKE_BUILD_TYPE=Release
	touch "$@"

$(CLANG_CXX_BUILD)/.headers: $(CLANG_CXX_BUILD)/.configured
	$(CLANG_ENV) "$(CLANG_CMAKE)" --build "$(@D)" --parallel "$(CLANG_JOBS)" --target generate-cxx-headers generate-cxxabi-headers
	touch "$@"

$(CLANG_BUILD)/probes/cxx-sdk.o: $(REPO_ROOT)/src/system-tests/clang/cxx-sdk.cpp $(CLANG_CXX_BUILD)/.headers $(SDK_HEADERS)
	mkdir -p "$(@D)"
	$(CLANG_CXX) --target=wasm32-unknown-unknown -std=c++17 -ffreestanding -fno-builtin \
		-fPIC -fno-exceptions -fno-rtti -nostdinc -nostdlib \
		-isystem "$(CLANG_CXX_BUILD)/include/c++/v1" \
		-isystem "$(SDK_ROOT)/usr/include" -isystem "$(SDK_ROOT)/usr/lib/waste/cc/include" \
		-c "$<" -o "$@"

$(CLANG_CXX_BUILD)/.compiled: $(CLANG_CXX_BUILD)/.headers
	$(CLANG_ENV) "$(CLANG_CMAKE)" --build "$(@D)" --parallel "$(CLANG_JOBS)" --target cxx cxxabi
	touch "$@"

$(CLANG_HOST_BUILD)/.configured: $(CLANG_STAGE) $(CLANG_PROFILES)
	mkdir -p "$(@D)"
	rm -f "$(@D)/CMakeCache.txt"
	"$(CLANG_CMAKE)" -S "$(CLANG_SOURCE)/llvm" -B "$(@D)" -G "Unix Makefiles" \
		-C "$(CLANG_DIR)/llvm.cmake" -DCMAKE_BUILD_TYPE=Release \
		$(CLANG_VCS_FLAGS) \
		-DCMAKE_C_COMPILER="$(CLANG_HOST_CC)" -DCMAKE_CXX_COMPILER="$(CLANG_CXX)"
	touch "$@"

$(CLANG_HOST_BUILD)/.compiled: $(CLANG_HOST_BUILD)/.configured
	"$(CLANG_CMAKE)" --build "$(@D)" --parallel "$(CLANG_JOBS)" --target llvm-tblgen clang-tblgen
	touch "$@"

$(CLANG_BUILD)/waste-crt.o: $(CRT_SOURCE) $(CLANG_DIR)/clang.mk
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WASM_CFLAGS) -DWASTE_MAIN_TWO_ARGS -c "$<" -o "$@"

$(CLANG_GUEST_BUILD)/.configured: $(CLANG_CXX_BUILD)/.compiled $(CLANG_HOST_BUILD)/.compiled $(CLANG_BUILD)/waste-crt.o $(CLANG_PROFILES) $(CLANG_DIR)/guest.cmake $(CLANG_DIR)/guest-toolchain.cmake $(CLANG_DIR)/include/machine/endian.h $(CLANG_DIR)/include/limits.h $(REPO_ROOT)/src/config.h
	mkdir -p "$(@D)"
	rm -f "$(@D)/CMakeCache.txt"
	$(CLANG_ENV) WASTE_CLANG_CXX_HEADERS="$(CLANG_CXX_BUILD)/include/c++/v1" \
		"$(CLANG_CMAKE)" -S "$(CLANG_SOURCE)/llvm" -B "$(@D)" -G "Unix Makefiles" \
		-C "$(CLANG_DIR)/llvm.cmake" -C "$(CLANG_DIR)/guest.cmake" \
		-DCMAKE_TOOLCHAIN_FILE="$(CLANG_DIR)/guest-toolchain.cmake" \
		-DCMAKE_BUILD_TYPE=MinSizeRel -DLLVM_NATIVE_TOOL_DIR="$(CLANG_HOST_BUILD)/bin" \
		$(CLANG_VCS_FLAGS) \
		-DLLVM_TABLEGEN="$(CLANG_HOST_BUILD)/bin/llvm-tblgen" \
		-DCLANG_TABLEGEN="$(CLANG_HOST_BUILD)/bin/clang-tblgen" \
		-DCMAKE_EXE_LINKER_FLAGS="$(CLANG_BUILD)/waste-crt.o -L$(CLANG_CXX_BUILD)/lib -lc++ -lc++abi -Wl,--no-entry,--import-memory,--import-table,--experimental-pic,--unresolved-symbols=import-dynamic,--export=_start,--export=main,--export=__heap_base,--export=__data_end,--export=__stack_pointer $(CLANG_NO_WASM_OPT)"
	touch "$@"

$(CLANG_GUEST_BUILD)/.compiled: $(CLANG_GUEST_BUILD)/.configured $(CLANG_DIR)/include/llvm/Support/ThreadPoolFuture.h $(CLANG_DIR)/include/llvm/Support/WasteBackoff.h $(CLANG_DIR)/include/llvm/Support/WasteLockFile.h $(CLANG_DIR)/include/llvm/Support/WasteMustache.h $(CLANG_DIR)/include/llvm/Support/WasteProgram.h $(CLANG_DIR)/include/clang/Basic/WasteSynchronization.h $(CLANG_DIR)/include/lld/Common/WasteDiagnostics.h $(CLANG_DIR)/include/clang/Lex/WasteTimestamp.h $(CLANG_DIR)/include/clang/Lex/WastePragma.h $(CLANG_DIR)/include/clang/Frontend/WasteSeedFile.h $(CLANG_DIR)/include/clang/Frontend/WasteFileInput.h
	# Check the guest platform library before compiling the large Clang graph.
	$(CLANG_ENV) "$(CLANG_CMAKE)" --build "$(@D)" --parallel "$(CLANG_JOBS)" --target LLVMSupport
	$(CLANG_ENV) "$(CLANG_CMAKE)" --build "$(@D)" --parallel "$(CLANG_JOBS)" --target clang lld
	touch "$@"

# Keep the named upstream artifact for diagnostics. The name custom section is
# optional executable metadata and accounts for 37.8 MiB in the first real link.
# Preserve exports, imports, code/data, dylink and target feature sections.
CLANG_OBJCOPY ?= llvm-objcopy
$(CLANG_BUILD)/clang-stripped.raw.wasm: $(CLANG_GUEST_BUILD)/.compiled $(CLANG_DIR)/clang.mk
	$(CLANG_OBJCOPY) --remove-section=name "$(CLANG_GUEST_BUILD)/bin/clang" "$@"

$(CLANG_BUILD)/clang.wasm: $(CLANG_BUILD)/clang-stripped.raw.wasm $(LIBC_REVIEW) $(SDK_ROOT)/lib/libc.so.wasm
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(SDK_ROOT)/lib/libc.so.wasm" \
		--rewrite "$<" --output "$@"

$(CLANG_BUILD)/wasm-ld.wasm: $(CLANG_GUEST_BUILD)/.compiled $(LIBC_REVIEW) $(SDK_ROOT)/lib/libc.so.wasm
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(SDK_ROOT)/lib/libc.so.wasm" \
		--rewrite "$(CLANG_GUEST_BUILD)/bin/lld" --output "$@"

install-clang: clang
	$(MAKE) clang-lld-no-lto
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$(CLANG_BUILD)/clang.wasm"
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(VFS_ROOT)/lib/libc.so.wasm" --check-imports "$(CLANG_BUILD)/wasm-ld-no-lto.wasm"
	$(MAKE) install-clang-sdk
	flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(VFS_TOOL)" install --root "$(VFS_ROOT)" --component clang --source "$(CLANG_BUILD)/clang.wasm"
	flock "$(BUILD_DIR)/.vfs-install.lock" $(PYTHON) "$(VFS_TOOL)" install --root "$(VFS_ROOT)" --component wasm-ld --source "$(CLANG_BUILD)/wasm-ld-no-lto.wasm"
	flock "$(BUILD_DIR)/.vfs-install.lock" bash -c 'mkdir -p "$$1/usr/share/licenses/clang" && install -m 644 "$$2/LICENSE.TXT" "$$1/usr/share/licenses/clang/LICENSE.TXT"' -- "$(VFS_ROOT)" "$(CLANG_SOURCE)"

test-clang: install-clang aux-test-runner
	bash "$(CLANG_DIR)/tests/run.sh" "$(REPO_ROOT)" "$(VFS_ROOT)" clang \
		"$(CLANG_DIR)/tests" "$(CLANG_BUILD)/tests" "$(abspath $(WASM_RUNNER))" "$(CLANG_DIR)/tests/setup.sh"

include $(CLANG_DIR)/lld-no-lto.mk
include $(CLANG_DIR)/sdk.mk
