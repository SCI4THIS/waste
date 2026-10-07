# Submodule build entry points.
#
# Checked-out submodules under this directory are read-only source
# dependencies. This Makefile stages their contents under build/ and runs
# the upstream build there; it must never mutate a submodule working tree.
# See ../docs/submodule-policy.md.

REPO_ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
BUILD_ROOT   := $(REPO_ROOT)/build
LOG_DIR      := $(BUILD_ROOT)/engine/logs

OCAML_SOURCE := $(REPO_ROOT)/submodules/wasm-spec/interpreter
OCAML_STAGE  := $(BUILD_ROOT)/ocaml-interpreter
OCAML_LOG    := $(LOG_DIR)/ocaml-reference.log

.PHONY: ocaml ocaml-test ocaml-clean

ocaml:
	@if [ ! -f "$(OCAML_SOURCE)/Makefile" ]; then \
	  echo "error: $(OCAML_SOURCE) is not populated; run 'git submodule update --init --recursive'" >&2; \
	  exit 1; \
	fi
	@mkdir -p "$(LOG_DIR)"
	@: >"$(OCAML_LOG)"
	@{ \
	  printf 'OCaml reference interpreter build\n'; \
	  printf 'Started: %s\n' "$$(date --iso-8601=seconds)"; \
	  printf 'Source: %s\n' "$(OCAML_SOURCE)"; \
	  printf 'Stage:  %s\n\n' "$(OCAML_STAGE)"; \
	} >>"$(OCAML_LOG)"
	rm -rf "$(OCAML_STAGE)"
	cp -r "$(OCAML_SOURCE)" "$(OCAML_STAGE)"
	@if ! $(MAKE) -C "$(OCAML_STAGE)" >>"$(OCAML_LOG)" 2>&1; then \
	  echo "error: build failed; see $(OCAML_LOG)" >&2; \
	  exit 1; \
	fi
	@printf '\nBuilt: %s\n' "$$(date --iso-8601=seconds)" >>"$(OCAML_LOG)"
	@printf 'OCaml reference interpreter: build passed.\n'
	@printf 'Log: %s\n' "$(OCAML_LOG)"

ocaml-test: ocaml
	@if ! $(MAKE) -C "$(OCAML_STAGE)" test >>"$(OCAML_LOG)" 2>&1; then \
	  echo "error: spec test suite failed; see $(OCAML_LOG)" >&2; \
	  exit 1; \
	fi
	@printf '\nTested: %s\n' "$$(date --iso-8601=seconds)" >>"$(OCAML_LOG)"
	@printf 'OCaml reference interpreter: spec test suite passed.\n'
	@printf 'Log: %s\n' "$(OCAML_LOG)"

ocaml-clean:
	rm -rf "$(OCAML_STAGE)"
