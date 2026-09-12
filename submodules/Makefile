SHELL := /bin/bash
.ONESHELL:
.SHELLFLAGS := -eu -o pipefail -c

REPO_ROOT := $(abspath ..)
SPEC_DIR := $(REPO_ROOT)/submodules/wasm-spec
INTERPRETER_DIR := $(SPEC_DIR)/interpreter
PATCH_FILE := $(REPO_ROOT)/submodules/wasm-spec-i31-int32.patch

BUILD_ROOT := $(REPO_ROOT)/build/ocaml-wasm
STAGING_DIR := $(BUILD_ROOT)/staging/interpreter
DIST_DIR := $(BUILD_ROOT)/dist
THREADED_DIST_DIR := $(BUILD_ROOT)/dist-threaded

SWITCH_NAME ?= waste-wasm
OPAM ?= opam

# Outputs
NATIVE_INTERPRETER := $(INTERPRETER_DIR)/_build/default/wasm.exe
SEQ_LOADER := $(DIST_DIR)/wasm_cli.bc.wasm.js
SEQ_ASSETS := $(DIST_DIR)/wasm_cli.bc.wasm.assets
CPS_LOADER := $(THREADED_DIST_DIR)/wasm_cli.bc.wasm.js
CPS_ASSETS := $(THREADED_DIST_DIR)/wasm_cli.bc.wasm.assets

.PHONY: all wasm native patch-status clean

all: wasm

# Each build is a transaction over the repository-owned OCaml patch. The EXIT
# trap restores the pinned submodule on successful builds, failed builds, and
# interrupted builds. An already-applied managed patch is also reverted when
# the transaction ends; an unrecognized patch state is never modified.
define begin_patch_transaction
	patch_active=false
	cleanup_patch() {
		status=$$?
		trap - EXIT
		if [[ "$$patch_active" == true ]]; then
			printf 'Reverting Wasm32 compatibility patch...\n'
			if ! git -C "$(SPEC_DIR)" apply --reverse "$(PATCH_FILE)"; then
				printf 'error: could not revert %s\n' "$(PATCH_FILE)" >&2
				status=1
			fi
		fi
		exit "$$status"
	}
	trap cleanup_patch EXIT
	if git -C "$(SPEC_DIR)" apply --check "$(PATCH_FILE)" >/dev/null 2>&1; then
		printf 'Applying Wasm32 compatibility patch...\n'
		git -C "$(SPEC_DIR)" apply "$(PATCH_FILE)"
		patch_active=true
	elif git -C "$(SPEC_DIR)" apply --reverse --check "$(PATCH_FILE)" >/dev/null 2>&1; then
		printf 'Wasm32 compatibility patch is already applied.\n'
		patch_active=true
	else
		printf 'error: Wasm32 compatibility patch cannot be applied or reverted cleanly\n' >&2
		exit 1
	fi
endef

patch-status:
	@if git -C "$(SPEC_DIR)" apply --check "$(PATCH_FILE)" >/dev/null 2>&1; then
		printf 'available\n'
	elif git -C "$(SPEC_DIR)" apply --reverse --check "$(PATCH_FILE)" >/dev/null 2>&1; then
		printf 'applied\n'
	else
		printf 'conflict\n'
		exit 1
	fi

# Build the native reference interpreter used as the differential oracle and
# as the validator for the OCaml Bash page.
native:
	@$(begin_patch_transaction)
	printf 'Building native OCaml reference interpreter...\n'
	cd "$(INTERPRETER_DIR)"
	$(OPAM) exec --switch="$(SWITCH_NAME)" -- dune build ./wasm.exe
	test -x "$(NATIVE_INTERPRETER)"

# Build sequential and CPS wasm_of_ocaml artifacts from a staging overlay. The
# patch is applied before the overlay is copied, and only the overlay is edited.
wasm:
	@$(begin_patch_transaction)
	printf 'Preparing OCaml-to-Wasm staging overlay...\n'
	rm -rf "$(STAGING_DIR)"
	mkdir -p "$(STAGING_DIR)" "$(DIST_DIR)" "$(THREADED_DIST_DIR)"
	cp -a "$(INTERPRETER_DIR)/." "$(STAGING_DIR)/"
	rm -rf "$(STAGING_DIR)/_build"

	sed -i 's/^(lang dune 2\.9)$$/(lang dune 3.17)/' "$(STAGING_DIR)/dune-project"
	sed -i 's/(modules :standard \\ main wasm wast smallint)/(modules :standard \\ main wasm_cli wast smallint)/' "$(STAGING_DIR)/dune"
	sed -i '/^(executable$$/ { n; s/^  (public_name wasm)$$/  (name wasm_cli)\n  (public_name wasm)/; }' "$(STAGING_DIR)/dune"
	sed -i '0,/^  (modules wasm)$$/s//  (modules wasm_cli)\n  (modes exe wasm)/' "$(STAGING_DIR)/dune"
	sed -i '0,/^  (libraries wasm)$$/s//  (libraries wasm)\n  (wasm_of_ocaml (javascript_files host\/control_runtime.js host\/control_runtime.wat))/' "$(STAGING_DIR)/dune"
	sed -i 's/(targets wasm\.ml)/(targets wasm_cli.ml)/' "$(STAGING_DIR)/dune"
	sed -i 's/wasm\.ml))$$/wasm_cli.ml))/' "$(STAGING_DIR)/dune"

	grep -Fqx '(lang dune 3.17)' "$(STAGING_DIR)/dune-project"
	grep -Fqx '  (modes exe wasm)' "$(STAGING_DIR)/dune"
	grep -Fqx '  (name wasm_cli)' "$(STAGING_DIR)/dune"
	grep -Fqx '  (wasm_of_ocaml (javascript_files host/control_runtime.js host/control_runtime.wat))' "$(STAGING_DIR)/dune"

	printf 'Building sequential OCaml-to-Wasm interpreter...\n'
	cd "$(STAGING_DIR)"
	$(OPAM) exec --switch="$(SWITCH_NAME)" -- \
		dune build --profile release ./wasm_cli.bc.wasm.js
	rm -f "$(SEQ_LOADER)"
	cp "$(STAGING_DIR)/_build/default/wasm_cli.bc.wasm.js" "$(DIST_DIR)/"
	rm -rf "$(SEQ_ASSETS)"
	cp -a "$(STAGING_DIR)/_build/default/wasm_cli.bc.wasm.assets" "$(DIST_DIR)/"

	printf 'Building CPS OCaml-to-Wasm interpreter...\n'
	sed -i 's#(wasm_of_ocaml (javascript_files host/control_runtime.js host/control_runtime.wat))#(wasm_of_ocaml (flags (:standard --effects cps)) (javascript_files host/control_runtime.js host/control_runtime.wat))#' "$(STAGING_DIR)/dune"
	grep -Fqx '  (wasm_of_ocaml (flags (:standard --effects cps)) (javascript_files host/control_runtime.js host/control_runtime.wat))' "$(STAGING_DIR)/dune"
	cd "$(STAGING_DIR)"
	$(OPAM) exec --switch="$(SWITCH_NAME)" -- \
		dune build --profile release ./wasm_cli.bc.wasm.js
	rm -f "$(CPS_LOADER)"
	cp "$(STAGING_DIR)/_build/default/wasm_cli.bc.wasm.js" "$(THREADED_DIST_DIR)/"
	rm -rf "$(CPS_ASSETS)"
	cp -a "$(STAGING_DIR)/_build/default/wasm_cli.bc.wasm.assets" "$(THREADED_DIST_DIR)/"

	test -f "$(SEQ_LOADER)"
	test -d "$(SEQ_ASSETS)"
	test -f "$(CPS_LOADER)"
	test -d "$(CPS_ASSETS)"

clean:
	rm -rf "$(STAGING_DIR)" "$(DIST_DIR)" "$(THREADED_DIST_DIR)"
