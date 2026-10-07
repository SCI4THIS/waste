PYTHON ?= python3
REPO_ROOT := $(abspath ../..)
BUILD_DIR ?= ../../build/libc-shared
BUILDER := ../html-rt/tools/build-libc-shared.py

.PHONY: all install
all:
	$(PYTHON) $(BUILDER) --repo-root $(REPO_ROOT) --output $(BUILD_DIR)

# Installation is explicit; packaging reads only the installed VFS snapshot.
install: all
	$(PYTHON) ../html-rt/tools/vfs.py install --component libc \
		--source $(BUILD_DIR)/libc.so.wasm
