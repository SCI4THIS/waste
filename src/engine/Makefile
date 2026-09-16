BISON ?= bison
FLEX ?= flex
BUILD_DIR ?= ../../build/engine
WAST_GEN_DIR := $(BUILD_DIR)/gen
WAST_TAB_C := $(WAST_GEN_DIR)/wast.tab.c
WAST_TAB_H := $(WAST_GEN_DIR)/wast.tab.h
WAST_LEX_C := $(WAST_GEN_DIR)/wast.lex.c
DISPATCH_GEN_C := $(WAST_GEN_DIR)/dispatch_gen.c
DISPATCH_GEN_H := $(WAST_GEN_DIR)/dispatch_gen.h

.PHONY: gen clean

gen: $(WAST_TAB_C) $(WAST_TAB_H) $(WAST_LEX_C) $(DISPATCH_GEN_C) $(DISPATCH_GEN_H)

# Generate Bison parser (tab.c and tab.h)
$(WAST_TAB_C) $(WAST_TAB_H): wat/parser.y
	mkdir -p $(WAST_GEN_DIR)
	$(BISON) --defines=$(WAST_TAB_H) -o $(WAST_TAB_C) wat/parser.y

# Generate Flex scanner
$(WAST_LEX_C): wat/lexer.l $(WAST_TAB_H)
	mkdir -p $(WAST_GEN_DIR)
	$(FLEX) -o $(WAST_LEX_C) wat/lexer.l

# Generate opcode dispatch table
$(DISPATCH_GEN_C) $(DISPATCH_GEN_H): op/opcodes.tab op/gen-dispatch.awk
	mkdir -p $(WAST_GEN_DIR)
	awk -v outdir=$(WAST_GEN_DIR) -f op/gen-dispatch.awk op/opcodes.tab

clean:
	rm -rf $(WAST_GEN_DIR)
