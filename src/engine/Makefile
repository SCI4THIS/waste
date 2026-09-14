BISON ?= bison
FLEX ?= flex
BUILD_DIR ?= ../../build/engine
WAST_GEN_DIR := $(BUILD_DIR)/gen
WAST_TAB_C := $(WAST_GEN_DIR)/wast.tab.c
WAST_TAB_H := $(WAST_GEN_DIR)/wast.tab.h
WAST_LEX_C := $(WAST_GEN_DIR)/wast.lex.c

.PHONY: gen clean

gen: $(WAST_TAB_C) $(WAST_TAB_H) $(WAST_LEX_C)

# Generate Bison parser (tab.c and tab.h)
$(WAST_TAB_C) $(WAST_TAB_H): text/wat_parser.y
	mkdir -p $(WAST_GEN_DIR)
	$(BISON) --defines=$(WAST_TAB_H) -o $(WAST_TAB_C) text/wat_parser.y

# Generate Flex scanner
$(WAST_LEX_C): text/wat_lexer.l $(WAST_TAB_H)
	mkdir -p $(WAST_GEN_DIR)
	$(FLEX) -o $(WAST_LEX_C) text/wat_lexer.l

clean:
	rm -rf $(WAST_GEN_DIR)
