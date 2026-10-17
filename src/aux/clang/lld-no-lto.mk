# Experimental object/archive linker, reusing the configured guest LLVM build.
# The full compiler and LTO-enabled linker remain separate comparison artifacts.
LLD_NO_LTO_BUILD := $(CLANG_BUILD)/lld-no-lto
LLD_NO_LTO_SOURCE := $(LLD_NO_LTO_BUILD)/source
LLD_NO_LTO_FLAGS := $(CLANG_GUEST_BUILD)/tools/lld/wasm/CMakeFiles/lldWasm.dir/flags.make
LLD_NO_LTO_LINK := $(CLANG_GUEST_BUILD)/tools/lld/tools/lld/CMakeFiles/lld.dir/link.txt
LLD_NO_LTO_LINK_DIR := $(CLANG_GUEST_BUILD)/tools/lld/tools/lld
LLD_NO_LTO_INPUTS := $(wildcard $(CLANG_SOURCE)/lld/wasm/*.cpp $(CLANG_SOURCE)/lld/wasm/*.h)
LLD_NO_LTO_NAMES := Driver InputChunks InputFiles MapFile MarkLive OutputSections OutputSegment Relocations SymbolTable Symbols SyntheticSections Writer WriterUtils
LLD_NO_LTO_OBJECTS := $(addprefix $(LLD_NO_LTO_BUILD)/obj-wasm/,$(addsuffix .o,$(LLD_NO_LTO_NAMES)))
LLD_NO_LTO_DEFINES = $(shell sed -n 's/^CXX_DEFINES = //p' "$(LLD_NO_LTO_FLAGS)")
LLD_NO_LTO_INCLUDES = $(shell sed -n 's/^CXX_INCLUDES = //p' "$(LLD_NO_LTO_FLAGS)")
LLD_NO_LTO_CXXFLAGS = $(shell sed -n 's/^CXX_FLAGS = //p' "$(LLD_NO_LTO_FLAGS)")
LLD_NO_LTO_ARCHIVES = $(foreach archive,$(filter-out %/liblldWasm.a,$(filter %.a,$(shell cat "$(LLD_NO_LTO_LINK)" 2>/dev/null))),$(abspath $(LLD_NO_LTO_LINK_DIR)/$(archive)))
LLD_NO_LTO_DRIVER_OBJECTS := $(LLD_NO_LTO_LINK_DIR)/CMakeFiles/lld.dir/lld.cpp.obj $(LLD_NO_LTO_LINK_DIR)/CMakeFiles/lld.dir/lld-driver.cpp.obj

.PHONY: clang-lld-no-lto
clang-lld-no-lto: $(CLANG_BUILD)/wasm-ld-no-lto.wasm

$(LLD_NO_LTO_SOURCE)/.prepared: $(LLD_NO_LTO_INPUTS) $(CLANG_DIR)/lld-no-lto.patch $(CLANG_DIR)/lld-shared-imports.patch
	mkdir -p "$(@D)"
	cp -a "$(CLANG_SOURCE)/lld/wasm/." "$(@D)/"
	chmod -R u+w "$(@D)"
	patch --batch --forward --fuzz=0 -d "$(@D)" -p1 < "$(CLANG_DIR)/lld-no-lto.patch"
	patch --batch --forward --fuzz=0 -d "$(@D)" -p1 < "$(CLANG_DIR)/lld-shared-imports.patch"
	touch "$@"

$(LLD_NO_LTO_OBJECTS): $(LLD_NO_LTO_BUILD)/obj-wasm/%.o: $(LLD_NO_LTO_SOURCE)/.prepared $(LLD_NO_LTO_FLAGS) $(SDK_HEADERS) $(CLANG_DIR)/lld-no-lto.mk
	mkdir -p "$(@D)"
	$(CLANG_CXX) --target=wasm32-unknown-unknown $(LLD_NO_LTO_DEFINES) -DLLD_WASTE_NO_LTO=1 -DLLD_WASTE_NAMED_IMPORTS=1 \
		-I"$(LLD_NO_LTO_SOURCE)" $(LLD_NO_LTO_INCLUDES) $(LLD_NO_LTO_CXXFLAGS) \
		-MMD -MP -c "$(LLD_NO_LTO_SOURCE)/$*.cpp" -o "$@"

$(LLD_NO_LTO_BUILD)/liblldWasm.a: $(LLD_NO_LTO_OBJECTS)
	rm -f "$@"
	$(CLANG_AR) rcs "$@" $^

$(LLD_NO_LTO_BUILD)/lld.raw.wasm: $(LLD_NO_LTO_BUILD)/liblldWasm.a $(LLD_NO_LTO_DRIVER_OBJECTS) $(LLD_NO_LTO_LINK) $(LLD_NO_LTO_ARCHIVES) $(CLANG_DIR)/lld-no-lto.mk $(CLANG_BUILD)/waste-crt.o $(CLANG_CXX_BUILD)/lib/libc++.a $(CLANG_CXX_BUILD)/lib/libc++abi.a
	$(CLANG_CXX) --target=wasm32-unknown-unknown -nostdlib $(CLANG_NO_WASM_OPT) \
		"$(CLANG_BUILD)/waste-crt.o" -L"$(CLANG_CXX_BUILD)/lib" -lc++ -lc++abi \
		-Wl,--no-entry,--import-memory,--import-table,--experimental-pic,--unresolved-symbols=import-dynamic,--export=_start,--export=main,--export=__heap_base,--export=__data_end,--export=__stack_pointer \
		-Wl,-Map,"$(LLD_NO_LTO_BUILD)/lld.map" $(LLD_NO_LTO_DRIVER_OBJECTS) \
		"$(LLD_NO_LTO_BUILD)/liblldWasm.a" $(LLD_NO_LTO_ARCHIVES) -o "$@"

$(CLANG_BUILD)/wasm-ld-no-lto.wasm: $(LLD_NO_LTO_BUILD)/lld.raw.wasm $(LIBC_REVIEW) $(SDK_ROOT)/lib/libc.so.wasm
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(SDK_ROOT)/lib/libc.so.wasm" --rewrite "$<" --output "$@"
	$(PYTHON) "$(LIBC_REVIEW)" --provider "$(SDK_ROOT)/lib/libc.so.wasm" --check-imports "$@"

-include $(wildcard $(LLD_NO_LTO_BUILD)/obj-wasm/*.d)
