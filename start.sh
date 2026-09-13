#!/usr/bin/env bash

set -u
set -o pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ -f "$SCRIPT_DIR/.gitmodules" ]]; then
  REPO_ROOT="$SCRIPT_DIR"
elif [[ -f "$SCRIPT_DIR/../.gitmodules" ]]; then
  REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
else
  REPO_ROOT="$SCRIPT_DIR"
fi
SPEC_DIR="$REPO_ROOT/submodules/wasm-spec"
INTERPRETER_DIR="$SPEC_DIR/interpreter"
I31_PATCH_FILE="$REPO_ROOT/submodules/wasm-spec-i31-int32.patch"
OCAML_BUILD_DIR="$REPO_ROOT/submodules"
ENGINE_BUILD="$REPO_ROOT/build/engine"
OCAML_BUILD="$REPO_ROOT/build/ocaml"
CLI_BUILD="$REPO_ROOT/build/cli-rt"
HTML_BUILD="$REPO_ROOT/build/html-rt"
NATIVE_INTERPRETER="$CLI_BUILD/waste-wast-ocaml"
BUILD_ROOT="$OCAML_BUILD"
DIST_DIR="$BUILD_ROOT/dist"
THREADED_DIST_DIR="$BUILD_ROOT/dist-threaded"
LOG_DIR="$ENGINE_BUILD/logs"
LOG_FILE="$LOG_DIR/build.log"
UPDATE_LOG="$LOG_DIR/update.log"
BROWSER_TEST_GENERATOR="$REPO_ROOT/src/html-rt/tools/generate-browser-tests.py"
BROWSER_TEST_HTML="$HTML_BUILD/test.html"
HTML_LOG="$LOG_DIR/html.log"
BASH_RUNTIME_BUILDER="$REPO_ROOT/src/html-rt/tools/build-bash-runtime.py"
BASH_HTML_GENERATOR="$REPO_ROOT/src/html-rt/tools/generate-bash-html.py"
BASH_RUNTIME_WAST="$OCAML_BUILD/bash-runtime.wast"
BASH_HTML="$HTML_BUILD/bash-ocaml.html"
BASH_HTML_LOG="$LOG_DIR/bash-html.log"
LIBC_OUTPUT="$HTML_BUILD/waste-libc/waste-libc.wasm"
LIBC_LOG="$LOG_DIR/libc-build.log"
TEST_LOG="$LOG_DIR/test.log"
C_ENGINE_RUNNER="$CLI_BUILD/waste-cli"
C_ENGINE_WASM="$HTML_BUILD/waste-wast.wasm"
C_ENGINE_GENERATOR="$REPO_ROOT/src/html-rt/tools/generate-c-engine-tests.py"
C_ENGINE_HTML="$HTML_BUILD/test.html"
C_ENGINE_CORE_HTML="$HTML_BUILD/test.html"
C_ENGINE_OCAML_LAYOUT_HTML="$HTML_BUILD/test.html"
C_ENGINE_HTML_LOG="$LOG_DIR/c-engine-html.log"
C_ENGINE_CORE_TESTS="$REPO_ROOT/submodules/wasm-spec/test/core"
C_ENGINE_RELAXED_SIMD_TESTS="$REPO_ROOT/submodules/wasm-spec/test/core/relaxed-simd"
C_ENGINE_MEMORY64_TESTS="$REPO_ROOT/submodules/wasm-spec/test/core/memory64"
C_ENGINE_BULK_MEMORY_TESTS="$REPO_ROOT/submodules/wasm-spec/test/core/bulk-memory"
C_ENGINE_DIY_POSIX_TESTS="$REPO_ROOT/tests/diy-posix-test"
C_ENGINE_BROWSER_TEST="$REPO_ROOT/tests/c-engine-browser-runtime.cjs"
C_ENGINE_BASH_GENERATOR="$REPO_ROOT/src/html-rt/tools/generate-c-engine-bash-html.py"
C_ENGINE_BASH_BROWSER_TEST="$REPO_ROOT/tests/c-engine-bash-browser-runtime.cjs"
C_ENGINE_BASH_RUNTIME_WAST="$HTML_BUILD/bash-runtime.wast"
C_ENGINE_BASH_HTML="$HTML_BUILD/bash.html"
C_ENGINE_BASH_LOG="$LOG_DIR/c-engine-bash.log"

mkdir -p "$LOG_DIR"

SWITCH_NAME="${WASTE_OCAML_SWITCH:-waste-wasm}"
OCAML_VERSION="${WASTE_OCAML_VERSION:-5.3.0}"
OPAM_PACKAGES=(dune menhir wasm_of_ocaml-compiler js_of_ocaml js_of_ocaml-ppx)

declare -a MISSING_SYSTEM=()
declare -a MISSING_OPAM=()
STATUS_TEXT=""

usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTION]

WASTE — WebAssembly Threading Environment build wizard.

  --sync           git pull/rebase with autostash and submodule update
  --check          print dependency status and exit
  --install-deps   interactively install missing dependencies

Engine (C):
  --cli-compile    build the C engine with the CLI runtime
  --cli-test       run the full core spec test suite via the CLI runner
  --html-test      generate the full C-engine browser test dashboard
  --html-bash      generate the self-contained C-engine Bash page

OCaml:
  --compile        compile the OCaml interpreter to Wasm
  --generate-html  generate the embedded browser test dashboard
  --generate-bash-html
                   generate the self-contained WASTE Bash page

Legacy / advanced:
  --build-libc     build waste-libc.wasm and its libc tests
  --c-engine-tests build C engine and run relaxed-SIMD spec tests
  --c-engine-html  alias for --html-test
  --c-engine-bash-html
                   alias for --html-bash
  --c-engine-core-tests
                   generate/run the C-engine core WAST browser dashboard
  --patch-status   show the Wasm32 compatibility patch status
  --apply-i31      apply the Wasm32 patch
  --revert-i31     revert the Wasm32 patch
  --update         alias for --sync
  --help           show this help

Environment overrides:
  WASTE_OCAML_SWITCH   opam switch name (default: $SWITCH_NAME)
  WASTE_OCAML_VERSION  OCaml version for a new switch (default: $OCAML_VERSION)
  WASTE_INSTRUCTION_QUANTUM
                       threaded scheduler quantum (default: 10000)
  WASTE_BASH_INSTRUCTION_QUANTUM
                       WASTE Bash scheduler quantum (default: 1000000)
EOF
}

have_command() {
  command -v "$1" >/dev/null 2>&1
}

version_at_least() {
  local actual="$1"
  local minimum="$2"
  [[ "$(printf '%s\n%s\n' "$minimum" "$actual" | sort -V | head -n 1)" == "$minimum" ]]
}

binaryen_is_usable() {
  have_command wasm-opt || return 1
  local version
  version="$(wasm-opt --version 2>/dev/null | grep -Eo '[0-9]+' | head -n 1)"
  [[ -n "$version" ]] && ((version >= 119))
}

wasm_ld_is_usable() {
  have_command wasm-ld ||
    [[ -x "$ENGINE_BUILD/toolchain/usr/bin/wasm-ld" ]]
}

switch_exists() {
  have_command opam &&
    opam switch list --short 2>/dev/null | grep -Fqx -- "$SWITCH_NAME"
}

opam_package_installed() {
  opam list --switch="$SWITCH_NAME" --installed --short "$1" 2>/dev/null |
    grep -Fqx -- "$1"
}

check_dependencies() {
  MISSING_SYSTEM=()
  MISSING_OPAM=()

  local command_name
  for command_name in git cc clang make opam bwrap whiptail wasm-as wasm-merge wasm-dis flex bison; do
    if ! have_command "$command_name"; then
      MISSING_SYSTEM+=("$command_name")
    fi
  done
  if ! wasm_ld_is_usable; then
    MISSING_SYSTEM+=("wasm-ld")
  fi
  if ! binaryen_is_usable; then
    MISSING_SYSTEM+=("wasm-opt>=119")
  fi

  local switch_status="missing"
  if switch_exists; then
    switch_status="present"
    local installed_ocaml
    installed_ocaml="$(opam exec --switch="$SWITCH_NAME" -- ocamlc -version 2>/dev/null || true)"
    if [[ -z "$installed_ocaml" ]] || ! version_at_least "$installed_ocaml" "4.14"; then
      MISSING_OPAM+=("OCaml>=4.14")
    fi
    local package_name
    for package_name in "${OPAM_PACKAGES[@]}"; do
      if ! opam_package_installed "$package_name"; then
        MISSING_OPAM+=("$package_name")
      fi
    done
  else
    MISSING_OPAM+=("OCaml switch $SWITCH_NAME ($OCAML_VERSION)")
    MISSING_OPAM+=("${OPAM_PACKAGES[@]}")
  fi

  local submodule_status="ready"
  if [[ ! -f "$INTERPRETER_DIR/dune-project" ]]; then
    submodule_status="empty"
  fi

  local system_status="all present"
  local opam_status="all present"
  ((${#MISSING_SYSTEM[@]})) && system_status="missing: ${MISSING_SYSTEM[*]}"
  ((${#MISSING_OPAM[@]})) && opam_status="missing: ${MISSING_OPAM[*]}"

  STATUS_TEXT="System tools: $system_status
opam switch: $SWITCH_NAME ($switch_status)
OCaml packages: $opam_status
./submodules/wasm-spec: $submodule_status

Target OCaml: $OCAML_VERSION
Output: $DIST_DIR"

  [[ "$submodule_status" == "ready" ]] &&
    ((${#MISSING_SYSTEM[@]} == 0)) &&
    ((${#MISSING_OPAM[@]} == 0))
}

package_manager() {
  if have_command pacman; then
    printf '%s\n' pacman
  elif have_command apt-get; then
    printf '%s\n' apt
  elif have_command dnf; then
    printf '%s\n' dnf
  elif have_command zypper; then
    printf '%s\n' zypper
  else
    printf '%s\n' unknown
  fi
}

system_install_command() {
  case "$(package_manager)" in
    pacman) printf '%s\n' "sudo pacman -S --needed git opam bubblewrap base-devel clang lld binaryen libnewt" ;;
    apt) printf '%s\n' "sudo apt-get install git opam bubblewrap build-essential clang lld binaryen whiptail" ;;
    dnf) printf '%s\n' "sudo dnf install git opam bubblewrap gcc clang lld make binaryen newt" ;;
    zypper) printf '%s\n' "sudo zypper install git opam bubblewrap gcc clang lld make binaryen newt" ;;
    *) printf '%s\n' "Install: opam, clang/lld, a C compiler, make, Binaryen 119+, and whiptail" ;;
  esac
}

confirm() {
  local prompt="$1"
  prompt="${prompt//\\n/$'\n'}"
  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    whiptail --title "OCaml Wasm setup" --yesno "$prompt" 12 76
    return $?
  fi

  local answer
  printf '%s [y/N] ' "$prompt" >&2
  read -r answer
  [[ "$answer" == "y" || "$answer" == "Y" || "$answer" == "yes" || "$answer" == "YES" ]]
}

show_message() {
  local title="$1"
  local message="$2"
  message="${message//\\n/$'\n'}"
  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    whiptail --title "$title" --msgbox "$message" 20 78
  else
    printf '\n%s\n%s\n\n' "$title" "$message"
  fi
}

progress_note() {
  printf '[%s] %s\n' "$(date '+%H:%M:%S')" "$1"
}

run_logged_step() {
  local label="$1"
  local log_file="$2"
  shift 2
  local started=$SECONDS
  local status

  progress_note "$label..."
  printf '\n== %s ==\n' "$label" >>"$log_file"
  if "$@" >>"$log_file" 2>&1; then
    status=0
    progress_note "$label: done ($((SECONDS - started))s)"
  else
    status=$?
    progress_note "$label: FAILED after $((SECONDS - started))s (see $log_file)"
  fi
  return "$status"
}

install_system_dependencies() {
  local manager
  local install_command
  manager="$(package_manager)"
  install_command="$(system_install_command)"

  if [[ "$manager" == "unknown" ]]; then
    show_message "Manual installation required" "$install_command"
    return 1
  fi

  if ! confirm "Install system dependencies with this command?\n\n$install_command"; then
    return 1
  fi

  case "$manager" in
    pacman) sudo pacman -S --needed git opam bubblewrap base-devel binaryen libnewt ;;
    apt) sudo apt-get install git opam bubblewrap build-essential binaryen whiptail ;;
    dnf) sudo dnf install git opam bubblewrap gcc make binaryen newt ;;
    zypper) sudo zypper install git opam bubblewrap gcc make binaryen newt ;;
  esac
}

install_opam_dependencies() {
  if ! have_command opam || ! binaryen_is_usable; then
    show_message "System dependencies missing" \
      "Install the system dependencies first.\n\n$(system_install_command)"
    return 1
  fi

  if ! opam var root >/dev/null 2>&1; then
    if ! confirm "Initialize opam in your user account?"; then
      return 1
    fi
    opam init --bare --yes || return 1
  fi

  if ! switch_exists; then
    if ! confirm "Create opam switch '$SWITCH_NAME' with OCaml $OCAML_VERSION?"; then
      return 1
    fi
    opam switch create "$SWITCH_NAME" "$OCAML_VERSION" --yes || return 1
  fi

  if ! confirm "Install these packages in '$SWITCH_NAME'?\n\n${OPAM_PACKAGES[*]}"; then
    return 1
  fi
  opam install --switch="$SWITCH_NAME" --yes "${OPAM_PACKAGES[@]}"
}

initialize_submodule() {
  if [[ -f "$INTERPRETER_DIR/dune-project" ]]; then
    return 0
  fi
  if ! confirm "The spec submodule is not initialized. Run git submodule update --init --recursive?"; then
    return 1
  fi
  git -C "$REPO_ROOT" submodule update --init --recursive
}

i31_patch_status() {
  if [[ ! -f "$INTERPRETER_DIR/runtime/i31.ml" ]]; then
    printf '%s\n' empty
  elif [[ ! -f "$I31_PATCH_FILE" ]]; then
    printf '%s\n' missing
  elif git -C "$SPEC_DIR" apply --check "$I31_PATCH_FILE" >/dev/null 2>&1; then
    printf '%s\n' available
  elif git -C "$SPEC_DIR" apply --reverse --check "$I31_PATCH_FILE" >/dev/null 2>&1; then
    printf '%s\n' applied
  else
    printf '%s\n' conflict
  fi
}

apply_i31_patch() {
  local state
  state="$(i31_patch_status)"
  case "$state" in
    applied)
      show_message "Wasm32 compatibility patch" "The patch is already applied."
      return 0 ;;
    available)
      if ! confirm "Apply the Wasm32 compatibility patch to the spec submodule?\n\n$I31_PATCH_FILE"; then
        return 1
      fi
      if git -C "$SPEC_DIR" apply "$I31_PATCH_FILE"; then
        show_message "Wasm32 compatibility patch" "Patch applied.\n\n./submodules/wasm-spec is now modified. Recompile to update the generated Wasm artifacts."
      else
        show_message "Wasm32 compatibility patch" "Patch application failed. The submodule was not changed."
        return 1
      fi ;;
    empty)
      show_message "Wasm32 compatibility patch" "The spec submodule is empty. Initialize it first."
      return 1 ;;
    missing)
      show_message "Wasm32 compatibility patch" "Patch file not found:\n$I31_PATCH_FILE"
      return 1 ;;
    conflict)
      show_message "Wasm32 compatibility patch" "The source differs from both the patched and unpatched versions. Resolve its changes before applying this patch."
      return 1 ;;
  esac
}

revert_i31_patch() {
  local state
  state="$(i31_patch_status)"
  case "$state" in
    available)
      show_message "Wasm32 compatibility patch" "The patch is not currently applied."
      return 0 ;;
    applied)
      if ! confirm "Revert the Wasm32 compatibility patch from the spec submodule?"; then
        return 1
      fi
      if git -C "$SPEC_DIR" apply --reverse "$I31_PATCH_FILE"; then
        show_message "Wasm32 compatibility patch" "Patch reverted.\n\n./submodules/wasm-spec is back to its upstream source. Recompile to update the generated Wasm artifacts."
      else
        show_message "Wasm32 compatibility patch" "Patch reversion failed."
        return 1
      fi ;;
    empty)
      show_message "Wasm32 compatibility patch" "The spec submodule is empty."
      return 1 ;;
    missing)
      show_message "Wasm32 compatibility patch" "Patch file not found:\n$I31_PATCH_FILE"
      return 1 ;;
    conflict)
      show_message "Wasm32 compatibility patch" "The source differs from both the patched and unpatched versions. It cannot be safely reverted automatically."
      return 1 ;;
  esac
}

i31_patch_menu() {
  while true; do
    local state
    local choice
    state="$(i31_patch_status)"
    choice="$(whiptail --title "Wasm32 compatibility patch" --menu \
      "Patch status: $state\n\nPatch: submodules/wasm-spec-i31-int32.patch" 19 82 4 \
      apply "Apply patch to the spec submodule" \
      revert "Revert patch from the spec submodule" \
      details "Explain the patch" \
      back "Return to the main menu" 3>&1 1>&2 2>&3)" || return 0

    case "$choice" in
      apply) apply_i31_patch || true ;;
      revert) revert_i31_patch || true ;;
      details)
        show_message "Wasm32 compatibility patch" \
          "Removes host-int assumptions that fail in a 32-bit Wasm target. It preserves unsigned i31 and u32 values, makes alignment validation shift-safe, and rejects unrepresentable local counts before allocation.\n\nApplying it intentionally makes the spec submodule dirty; reverting restores the pinned source." ;;
      back) return 0 ;;
    esac
  done
}

restore_i31_patch_after_update() {
  local state
  state="$(i31_patch_status)"
  case "$state" in
    available)
      git -C "$SPEC_DIR" apply "$I31_PATCH_FILE"
      printf 'Wasm32 patch: reapplied\n' >>"$UPDATE_LOG" ;;
    applied)
      printf 'Wasm32 patch: already present in the updated source\n' >>"$UPDATE_LOG" ;;
    *)
      printf 'Wasm32 patch: could not restore automatically (status: %s)\n' "$state" >>"$UPDATE_LOG"
      return 1 ;;
  esac
}

safe_repository_update() {
  local original_patch_state
  local patch_was_reverted=false
  original_patch_state="$(i31_patch_status)"

  if [[ "$original_patch_state" == "conflict" || "$original_patch_state" == "missing" ]]; then
    show_message "Safe repository update" \
      "Cannot update safely while the Wasm32 patch status is '$original_patch_state'. Resolve the patch state first."
    return 1
  fi

  if ! confirm "Perform a safe repository update?\n\n1. Temporarily revert the managed Wasm32 patch if needed\n2. git pull --rebase --autostash\n3. git submodule update --init --recursive\n4. Reapply the patch if upstream does not contain it\n\nNote: Git autostash does not include untracked files."; then
    return 1
  fi

  if [[ "$original_patch_state" != "empty" ]] &&
     [[ -n "$(git -C "$SPEC_DIR" status --porcelain 2>/dev/null)" ]]; then
    if [[ "$original_patch_state" != "applied" ]]; then
      show_message "Safe repository update" \
        "The spec submodule contains changes other than the managed Wasm32 patch. Commit, stash, or revert them before updating."
      return 1
    fi

    if ! git -C "$SPEC_DIR" apply --reverse "$I31_PATCH_FILE"; then
      show_message "Safe repository update" "Could not temporarily revert the Wasm32 patch."
      return 1
    fi
    patch_was_reverted=true

    if [[ -n "$(git -C "$SPEC_DIR" status --porcelain 2>/dev/null)" ]]; then
      git -C "$SPEC_DIR" apply "$I31_PATCH_FILE" || true
      show_message "Safe repository update" \
        "The submodule also contains unmanaged changes. The Wasm32 patch was restored; commit, stash, or revert the other changes before updating."
      return 1
    fi
  fi

  : >"$UPDATE_LOG"
  {
    printf 'WASTE safe repository update\n'
    printf 'Started: %s\n' "$(date --iso-8601=seconds)"
    printf 'Original Wasm32 patch state: %s\n\n' "$original_patch_state"
    printf '$ git pull --rebase --autostash\n'
  } >>"$UPDATE_LOG"

  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    whiptail --title "Safe repository update" --infobox \
      "Pulling and rebasing the main repository...\n\nLog: $UPDATE_LOG" 9 76
  fi

  if ! git -C "$REPO_ROOT" pull --rebase --autostash >>"$UPDATE_LOG" 2>&1; then
    if [[ "$patch_was_reverted" == true ]]; then
      restore_i31_patch_after_update || true
    fi
    show_message "Safe repository update failed" \
      "git pull failed. The managed patch was restored when possible.\n\nLog: $UPDATE_LOG"
    return 1
  fi

  printf '\n$ git submodule update --init --recursive\n' >>"$UPDATE_LOG"
  if ! git -C "$REPO_ROOT" submodule update --init --recursive >>"$UPDATE_LOG" 2>&1; then
    if [[ "$patch_was_reverted" == true ]]; then
      restore_i31_patch_after_update || true
    fi
    show_message "Safe repository update incomplete" \
      "The main repository updated, but the submodule update failed. The managed patch was restored when possible.\n\nLog: $UPDATE_LOG"
    return 1
  fi

  if [[ "$patch_was_reverted" == true ]]; then
    if ! restore_i31_patch_after_update; then
      show_message "Repository updated; patch needs attention" \
        "The repository and submodule updated, but the Wasm32 patch could not be reapplied automatically.\n\nPatch status: $(i31_patch_status)\nLog: $UPDATE_LOG"
      return 1
    fi
  fi

  {
    printf '\nCompleted: %s\n' "$(date --iso-8601=seconds)"
    printf 'Current Wasm32 patch state: %s\n' "$(i31_patch_status)"
  } >>"$UPDATE_LOG"

  show_message "Safe repository update complete" \
    "The repository and submodules are updated.\n\nWasm32 patch: $(i31_patch_status)\nLog: $UPDATE_LOG"
}

install_missing_dependencies() {
  check_dependencies || true
  if ((${#MISSING_SYSTEM[@]})); then
    install_system_dependencies || return 1
  fi
  check_dependencies || true
  if ((${#MISSING_OPAM[@]})); then
    install_opam_dependencies || return 1
  fi
  initialize_submodule || return 1
  check_dependencies
}

build_ocaml_wasm() {
  make -C "$OCAML_BUILD_DIR" SWITCH_NAME="$SWITCH_NAME" wasm
}

build_ocaml_native() {
  make -C "$OCAML_BUILD_DIR" SWITCH_NAME="$SWITCH_NAME" native
}

compile_interpreter() {
  local compile_patch_state
  compile_patch_state="$(i31_patch_status)"
  : >"$LOG_FILE"
  {
    printf 'WASTE OCaml-to-Wasm build\n'
    printf 'Started: %s\n' "$(date --iso-8601=seconds)"
    printf 'Repository: %s\n' "$REPO_ROOT"
    printf 'Spec source: %s\n' "$INTERPRETER_DIR"
    printf 'opam switch: %s\n\n' "$SWITCH_NAME"
    printf 'Wasm32 patch: %s\n\n' "$compile_patch_state"
  } >>"$LOG_FILE"

  if ! check_dependencies; then
    printf '%s\n' "$STATUS_TEXT" >>"$LOG_FILE"
    show_message "Dependencies incomplete" "$STATUS_TEXT"
    return 1
  fi

  mkdir -p -- "$BUILD_ROOT"

  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    whiptail --title "OCaml Wasm build" --infobox \
      "Preparing an overlay and compiling the reference interpreter...\n\nLog: $LOG_FILE" 10 76
  else
    printf 'Compiling the OCaml reference interpreter to Wasm...\n'
  fi

  if ! run_logged_step "Build sequential and CPS OCaml-to-Wasm interpreters" \
      "$LOG_FILE" build_ocaml_wasm; then
    show_message "Build failed" \
      "The patched OCaml-to-Wasm build failed. The Makefile attempted to restore the spec submodule.\n\nPatch status: $(i31_patch_status)\nLog: $LOG_FILE"
    return 1
  fi

  {
    printf '\nCompleted: %s\n' "$(date --iso-8601=seconds)"
    printf 'Final Wasm32 patch state: %s\n' "$(i31_patch_status)"
    printf 'Loader: %s\n' "$DIST_DIR/wasm_cli.bc.wasm.js"
    printf 'Assets: %s\n' "$DIST_DIR/wasm_cli.bc.wasm.assets"
    printf 'Threaded loader: %s\n' "$THREADED_DIST_DIR/wasm_cli.bc.wasm.js"
    printf 'Threaded assets: %s\n' "$THREADED_DIST_DIR/wasm_cli.bc.wasm.assets"
  } >>"$LOG_FILE"

  show_message "Build complete" \
    "The Makefile temporarily applied the Wasm32 patch, built both OCaml-to-Wasm variants, and restored the spec submodule.\n\nSequential: $DIST_DIR/wasm_cli.bc.wasm.js\nThreaded CPS: $THREADED_DIST_DIR/wasm_cli.bc.wasm.js\nLog: $LOG_FILE"
}

build_waste_libc() {
  local quiet="${1:-false}"
  mkdir -p -- "$(dirname -- "$LIBC_OUTPUT")"
  : >"$LIBC_LOG"
  {
    printf 'WASTE guest libc build\n'
    printf 'Started: %s\n' "$(date --iso-8601=seconds)"
    printf 'Source: %s\n' "$REPO_ROOT/src/html-rt/lib/stdlib.wat"
    printf 'Output: %s\n\n' "$LIBC_OUTPUT"
  } >>"$LIBC_LOG"

  if ! have_command python3 || ! have_command clang || ! wasm_ld_is_usable || ! have_command wasm-as ||
     ! have_command wasm-merge || ! have_command wasm-dis; then
    printf 'error: Python 3, clang, wasm-as, wasm-merge, and wasm-dis are required\n' >>"$LIBC_LOG"
    if [[ "$quiet" != true ]]; then
      show_message "Guest libc build failed" "Python 3, clang, and Binaryen are required.\n\nLog: $LIBC_LOG"
    fi
    return 1
  fi
  if ! run_logged_step "Build guest libc and fixtures" "$LIBC_LOG" \
      make -C "$REPO_ROOT/src/html-rt" BUILD_DIR="$HTML_BUILD" \
      ENGINE_BUILD_DIR="$ENGINE_BUILD" waste-libc; then
    if [[ "$quiet" != true ]]; then
      show_message "Guest libc build failed" "Could not build waste-libc.wasm.\n\nLog: $LIBC_LOG"
    fi
    return 1
  fi
  printf 'Completed: %s\n' "$(date --iso-8601=seconds)" >>"$LIBC_LOG"
  if [[ "$quiet" != true ]]; then
    show_message "Guest libc build complete" \
      "The shared-memory guest libc module and browser fixtures were generated.\n\nOutput: $LIBC_OUTPUT\nLog: $LIBC_LOG"
  fi
}

generate_browser_test_html() {
  local quantum="${WASTE_INSTRUCTION_QUANTUM:-10000}"
  local output="$BROWSER_TEST_HTML"
  local legacy_output="$BUILD_ROOT/browser-tests-threaded.html"
  local html_log="$HTML_LOG"
  local loader_dist="$THREADED_DIST_DIR"
  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    quantum="$(whiptail --title "Cooperative scheduler" --inputbox \
      "Interpreter steps per test before switching:" 9 64 "$quantum" \
      3>&1 1>&2 2>&3)" || return 1
  fi
  if [[ ! "$quantum" =~ ^[1-9][0-9]*$ ]]; then
    show_message "Browser test dashboard" "Instruction quantum must be a positive integer."
    return 1
  fi

  : >"$html_log"
  {
    printf 'WASTE browser test dashboard generation\n'
    printf 'Started: %s\n' "$(date --iso-8601=seconds)"
    printf 'Output: %s\n' "$output"
    printf 'Instruction quantum: %s\n\n' "$quantum"
  } >>"$html_log"

  if ! have_command python3 || ! have_command make || ! have_command opam; then
    printf 'error: Python 3, make, and opam are required\n' >>"$html_log"
    show_message "Browser test dashboard" \
      "Python 3, make, and opam are required to build and generate the HTML.\n\nLog: $html_log"
    return 1
  fi
  if [[ ! -f "$BROWSER_TEST_GENERATOR" ]]; then
    printf 'error: generator is missing: %s\n' "$BROWSER_TEST_GENERATOR" >>"$html_log"
    show_message "Browser test dashboard" "The HTML generator is missing.\n\nLog: $html_log"
    return 1
  fi
  if ! run_logged_step "Build OCaml-to-Wasm interpreter" "$html_log" \
      build_ocaml_wasm; then
    show_message "Browser test dashboard" \
      "The OCaml-to-Wasm build failed. The Makefile attempted to restore the spec submodule.\n\nPatch status: $(i31_patch_status)\nLog: $html_log"
    return 1
  fi
  if [[ ! -f "$loader_dist/wasm_cli.bc.wasm.js" ]]; then
    printf 'error: compiled Wasm loader is missing after the build\n' >>"$html_log"
    show_message "Browser test dashboard" \
      "The OCaml build completed without the expected CPS loader.\n\nLog: $html_log"
    return 1
  fi
  if ! build_waste_libc true; then
    printf 'error: waste-libc build failed; see %s\n' "$LIBC_LOG" >>"$html_log"
    show_message "Browser test dashboard" "The guest libc must build before its tests can be embedded.\n\nLog: $LIBC_LOG"
    return 1
  fi

  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    whiptail --title "Browser test dashboard" --infobox \
      "Embedding the compiled OCaml Wasm and all specification tests...\n\nLog: $html_log" 9 78
  else
    printf 'Generating embedded browser test dashboard...\n'
  fi

  if ! run_logged_step "Embed interpreter and test suites" "$html_log" \
      python3 "$BROWSER_TEST_GENERATOR" --repo-root "$REPO_ROOT" \
      --quantum "$quantum" --output "$output"; then
    show_message "Browser test dashboard failed" "HTML generation failed.\n\nLog: $html_log"
    return 1
  fi
  if [[ -f "$legacy_output" ]]; then
    rm -- "$legacy_output"
    printf 'Removed obsolete output: %s\n' "$legacy_output" >>"$html_log"
  fi

  show_message "Browser test dashboard generated" \
    "A self-contained HTML dashboard was generated with the OCaml Wasm and all .wast tests embedded.\n\nOutput: $output\nLog: $html_log"
}

generate_bash_html() {
  local quantum="${WASTE_BASH_INSTRUCTION_QUANTUM:-1000000}"
  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    quantum="$(whiptail --title "WASTE Bash" --inputbox \
      "Interpreter steps before yielding to the browser:" 9 64 "$quantum" \
      3>&1 1>&2 2>&3)" || return 1
  fi
  if [[ ! "$quantum" =~ ^[1-9][0-9]*$ ]]; then
    show_message "WASTE Bash" "Instruction quantum must be a positive integer."
    return 1
  fi

  : >"$BASH_HTML_LOG"
  {
    printf 'WASTE Bash static HTML generation\n'
    printf 'Started: %s\n' "$(date --iso-8601=seconds)"
    printf 'Output: %s\n' "$BASH_HTML"
    printf 'Instruction quantum: %s\n\n' "$quantum"
  } >>"$BASH_HTML_LOG"

  if ! have_command python3 || ! have_command opam || ! have_command clang || ! have_command wasm-as ||
     ! have_command wasm-merge || ! have_command wasm-dis || ! wasm_ld_is_usable; then
    printf 'error: Python 3, opam, clang/lld, and Binaryen are required\n' >>"$BASH_HTML_LOG"
    show_message "WASTE Bash" "Python 3, opam, clang/lld, and Binaryen are required.\n\nLog: $BASH_HTML_LOG"
    return 1
  fi
  if [[ ! -f "$REPO_ROOT/examples/bash.wat" || ! -f "$BASH_RUNTIME_BUILDER" ||
        ! -f "$BASH_HTML_GENERATOR" ]]; then
    printf 'error: Bash source or generator is missing\n' >>"$BASH_HTML_LOG"
    show_message "WASTE Bash" "Bash source or a generation tool is missing.\n\nLog: $BASH_HTML_LOG"
    return 1
  fi

  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    whiptail --title "WASTE Bash" --infobox \
      "Relinking and validating Bash, then embedding the CPS interpreter...\n\nLog: $BASH_HTML_LOG" 9 78
  else
    printf 'Generating self-contained WASTE Bash page...\n'
  fi
  if ! run_logged_step "Build OCaml-to-Wasm interpreter" "$BASH_HTML_LOG" \
      build_ocaml_wasm ||
     ! run_logged_step "Build native OCaml validation oracle" "$BASH_HTML_LOG" \
      build_ocaml_native ||
     ! run_logged_step "Relink the interactive Bash runtime" "$BASH_HTML_LOG" \
      python3 "$BASH_RUNTIME_BUILDER" --repo-root "$REPO_ROOT" \
      --interactive --output "$BASH_RUNTIME_WAST" ||
     ! run_logged_step "Generate the self-contained Bash page" "$BASH_HTML_LOG" \
      python3 "$BASH_HTML_GENERATOR" --repo-root "$REPO_ROOT" \
      --launch "$BASH_RUNTIME_WAST" --output "$BASH_HTML" --quantum "$quantum" \
      --validator "$NATIVE_INTERPRETER"; then
    show_message "WASTE Bash generation failed" "Could not generate the static page.\n\nLog: $BASH_HTML_LOG"
    return 1
  fi
  printf 'Completed: %s\n' "$(date --iso-8601=seconds)" >>"$BASH_HTML_LOG"
  show_message "WASTE Bash generated" \
    "The static page embeds the CPS interpreter, shared runtime, waste-libc, and Bash. It can be opened directly with file:// and requires no server.\n\nOutput: $BASH_HTML\nLog: $BASH_HTML_LOG"
}

run_logged_test() {
  local label="$1"
  shift
  run_logged_step "$label" "$TEST_LOG" "$@"
}

run_official_core_tests() {
  run_logged_step "Official WebAssembly core suite" "$TEST_LOG" \
    python3 "$SPEC_DIR/test/core/run.py" \
    --wasm "$INTERPRETER_DIR/_build/default/wasm.exe"
}

run_official_tail_call_tests() {
  local -a files=(
    "$SPEC_DIR/test/core/return_call.wast"
    "$SPEC_DIR/test/core/return_call_indirect.wast"
    "$SPEC_DIR/test/core/return_call_ref.wast"
  )
  printf '\n== Official WebAssembly tail-call tests ==\n' >>"$TEST_LOG"
  printf 'Files:\n' >>"$TEST_LOG"
  printf '  %s\n' "${files[@]}" >>"$TEST_LOG"
  local file status=0 started
  for file in "${files[@]}"; do
    started=$SECONDS
    if ! run_logged_step "Tail-call test: ${file##*/}" "$TEST_LOG" \
        python3 "$SPEC_DIR/test/core/run.py" \
        --wasm "$INTERPRETER_DIR/_build/default/wasm.exe" \
        "$file"; then
      status=1
    fi
    printf 'Duration: %ds\n' "$((SECONDS - started))" >>"$TEST_LOG"
  done
  return "$status"
}

run_tail_call_smoke() {
  local started
  printf '\n== Short tail-call smoke benchmark ==\n' >>"$TEST_LOG"
  started=$SECONDS
  run_logged_step "Short tail-call smoke benchmark" "$TEST_LOG" \
    "$NATIVE_INTERPRETER" "$REPO_ROOT/tests/tail-call-smoke.wast"
  local status=$?
  printf 'Execution duration: %ds\n' "$((SECONDS - started))" >>"$TEST_LOG"
  return "$status"
}

run_sandbox_isolation_tests() {
  local -a isolation_files=(
    "$REPO_ROOT/tests/diy-posix-test/spectest-isolation-a.wast"
    "$REPO_ROOT/tests/diy-posix-test/spectest-isolation-b.wast"
  )
  local -a contamination_files=(
    "$SPEC_DIR/test/core/data.wast"
    "$SPEC_DIR/test/core/imports.wast"
  )
  local -a isolation_args=()
  local -a contamination_args=()
  local file
  for file in "${isolation_files[@]}"; do
    isolation_args+=(-i "$file")
  done
  for file in "${contamination_files[@]}"; do
    contamination_args+=(-i "$file")
  done
  run_logged_step "Scheduled sandbox isolation probes" "$TEST_LOG" \
    "$NATIVE_INTERPRETER" -ca --schedule --threads 2 -q 1 \
    "${isolation_args[@]}" || return 1
  run_logged_step "Scheduled cross-test contamination probes" "$TEST_LOG" \
    "$NATIVE_INTERPRETER" -ca --schedule --threads 2 -q 10000 \
    "${contamination_args[@]}"
}

compile_cli_engine() {
  if ! have_command cc || ! have_command make || ! have_command flex || ! have_command bison; then
    show_message "CLI engine compile" \
      "cc, make, flex, and bison are required."
    return 1
  fi
  mkdir -p "$ENGINE_BUILD" "$CLI_BUILD"
  : >"$C_ENGINE_HTML_LOG"
  {
    printf 'CLI engine compile\n'
    printf 'Started: %s\n\n' "$(date --iso-8601=seconds)"
  } >>"$C_ENGINE_HTML_LOG"

  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    whiptail --title "CLI engine compile" --infobox \
      "Building the C engine with the CLI runtime...\n\nLog: $C_ENGINE_HTML_LOG" 9 78
  else
    printf 'Building the C engine with the CLI runtime...\n'
  fi

  if ! run_logged_step "Build native C engine" "$C_ENGINE_HTML_LOG" \
      make -C "$REPO_ROOT/src/cli-rt" BUILD_DIR="$CLI_BUILD" \
      ENGINE_BUILD_DIR="$ENGINE_BUILD" \
      wast-native; then
    show_message "CLI engine compile failed" \
      "The build failed.\n\nLog: $C_ENGINE_HTML_LOG"
    return 1
  fi
  printf 'Completed: %s\n' "$(date --iso-8601=seconds)" >>"$C_ENGINE_HTML_LOG"
  show_message "CLI engine compile" \
    "The native WAST runner was built successfully.\n\nOutput: $C_ENGINE_RUNNER\nLog: $C_ENGINE_HTML_LOG"
}

run_cli_tests() {
  if ! have_command cc || ! have_command make || ! have_command flex || ! have_command bison; then
    show_message "CLI test suite" \
      "cc, make, flex, and bison are required."
    return 1
  fi
  if [[ ! -d "$C_ENGINE_CORE_TESTS" ]]; then
    show_message "CLI test suite" \
      "Core tests not found. Initialize the wasm-spec submodule first."
    return 1
  fi

  : >"$TEST_LOG"
  {
    printf 'CLI engine full test suite\n'
    printf 'Started: %s\n\n' "$(date --iso-8601=seconds)"
  } >>"$TEST_LOG"

  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    whiptail --title "CLI test suite" --infobox \
      "Building the engine and running all core WAST spec tests...\n\nLog: $TEST_LOG" 9 78
  else
    printf 'Running CLI test suite; log: %s\n' "$TEST_LOG"
  fi

  if ! run_logged_step "Build native C engine" "$TEST_LOG" \
      make -C "$REPO_ROOT/src/cli-rt" BUILD_DIR="$CLI_BUILD" \
      ENGINE_BUILD_DIR="$ENGINE_BUILD" \
      wast-native; then
    show_message "CLI test suite failed" \
      "The engine build failed.\n\nLog: $TEST_LOG"
    return 1
  fi

  local total=0 passed=0 failed=0 status=0 total_files=0
  local file
  for file in "$C_ENGINE_CORE_TESTS"/*.wast; do
    [[ -f "$file" ]] && ((total_files++))
  done
  progress_note "Run $total_files core WAST files..."
  for file in "$C_ENGINE_CORE_TESTS"/*.wast; do
    [[ -f "$file" ]] || continue
    local name="${file##*/}"
    local file_started=$SECONDS
    printf '[%3d/%3d] %-32s ' "$((total + 1))" "$total_files" "$name"
    printf '  %s ... ' "$name" >>"$TEST_LOG"
    if "$C_ENGINE_RUNNER" "$file" >>"$TEST_LOG" 2>&1; then
      printf 'ok\n' >>"$TEST_LOG"
      ((passed++))
      printf 'ok (%ds)\n' "$((SECONDS - file_started))"
    else
      printf 'FAIL\n' >>"$TEST_LOG"
      ((failed++))
      status=1
      printf 'FAIL (%ds; see %s)\n' "$((SECONDS - file_started))" "$TEST_LOG"
    fi
    ((total++))
  done
  progress_note "Core WAST files complete: $passed passed, $failed failed"

  printf '\nFinished: %s\n' "$(date --iso-8601=seconds)" >>"$TEST_LOG"
  printf 'Results: %d/%d passed, %d failed\n' "$passed" "$total" "$failed" >>"$TEST_LOG"

  if ((status == 0)); then
    show_message "CLI test suite passed" \
      "$passed/$total spec tests passed.\n\nLog: $TEST_LOG"
  else
    show_message "CLI test suite failed" \
      "$passed/$total passed, $failed failed.\n\nLog: $TEST_LOG"
  fi
  return "$status"
}

generate_c_engine_tests() {
  if ! have_command cc || ! have_command make || ! have_command flex ||
      ! have_command bison || ! have_command python3 || ! have_command node; then
    printf 'error: cc, make, flex, bison, python3, and node are required for C engine tests\n' >>"$TEST_LOG"
    return 1
  fi
  if [[ ! -d "$C_ENGINE_RELAXED_SIMD_TESTS" ]]; then
    printf 'error: relaxed-simd tests not found at %s (run: git submodule update --init)\n' \
      "$C_ENGINE_RELAXED_SIMD_TESTS" >>"$TEST_LOG"
    return 1
  fi
  printf '\n== C engine browser tests (relaxed-SIMD and DIY POSIX) ==\n' >>"$TEST_LOG"
  run_logged_step "Build native C engine" "$TEST_LOG" \
    make -C "$REPO_ROOT/src/cli-rt" \
    BUILD_DIR="$CLI_BUILD" ENGINE_BUILD_DIR="$ENGINE_BUILD" \
    wast-native || return 1
  run_logged_step "Build browser C engine" "$TEST_LOG" \
    make -C "$REPO_ROOT/src/html-rt" \
    BUILD_DIR="$HTML_BUILD" ENGINE_BUILD_DIR="$ENGINE_BUILD" \
    wast-browser || return 1
  run_logged_step "Generate relaxed-SIMD/POSIX browser dashboard" "$TEST_LOG" \
    python3 "$C_ENGINE_GENERATOR" \
    --runner "$C_ENGINE_RUNNER" \
    --wasm "$C_ENGINE_WASM" \
    --tests "$C_ENGINE_RELAXED_SIMD_TESTS" \
    --tests "$C_ENGINE_MEMORY64_TESTS" \
    --tests "$C_ENGINE_BULK_MEMORY_TESTS" \
    --tests "$C_ENGINE_DIY_POSIX_TESTS" \
    --output "$C_ENGINE_HTML" || return 1
  run_logged_step "Exercise generated browser dashboard" "$TEST_LOG" \
    node "$C_ENGINE_BROWSER_TEST" "$C_ENGINE_HTML"
}

generate_c_engine_dashboard_html() {
  if ! have_command cc || ! have_command clang || ! have_command make ||
      ! have_command flex || ! have_command bison || ! have_command python3 ||
      ! have_command node ||
      ! have_command wasm-as || ! wasm_ld_is_usable; then
    show_message "C-engine browser dashboard" \
      "cc, clang, make, flex, bison, wasm-ld, wasm-as, and Python 3 are required."
    return 1
  fi
  if [[ ! -d "$C_ENGINE_CORE_TESTS" || ! -d "$C_ENGINE_DIY_POSIX_TESTS" ]]; then
    show_message "C-engine browser dashboard" \
      "Specification or DIY POSIX tests are missing. Initialize the wasm-spec submodule first."
    return 1
  fi
  if ! build_waste_libc true; then
    show_message "C-engine browser dashboard" \
      "The guest libc and its generated tests must build first.\n\nLog: $LIBC_LOG"
    return 1
  fi

  mkdir -p "$ENGINE_BUILD" "$CLI_BUILD" "$HTML_BUILD"
  : >"$C_ENGINE_HTML_LOG"
  {
    printf 'WASTE C-engine OCaml-layout dashboard generation\n'
    printf 'Started: %s\n' "$(date --iso-8601=seconds)"
    printf 'Output: %s\n\n' "$C_ENGINE_OCAML_LAYOUT_HTML"
  } >>"$C_ENGINE_HTML_LOG"

  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    whiptail --title "C-engine browser dashboard" --infobox \
      "Building the C engine and embedding the specification, signaling/POSIX, and libc tests...\n\nLog: $C_ENGINE_HTML_LOG" 10 84
  else
    printf 'Generating C-engine browser dashboard in OCaml-Wasm layout...\n'
  fi

  if ! run_logged_step "Build native C engine" "$C_ENGINE_HTML_LOG" \
      make -C "$REPO_ROOT/src/cli-rt" BUILD_DIR="$CLI_BUILD" \
      ENGINE_BUILD_DIR="$ENGINE_BUILD" \
      wast-native ||
     ! run_logged_step "Build browser C engine" "$C_ENGINE_HTML_LOG" \
      make -C "$REPO_ROOT/src/html-rt" BUILD_DIR="$HTML_BUILD" \
      ENGINE_BUILD_DIR="$ENGINE_BUILD" \
      wast-browser; then
    show_message "C-engine browser dashboard failed" \
      "The C engine build failed.\n\nLog: $C_ENGINE_HTML_LOG"
    return 1
  fi
  if ! run_logged_step "Embed specification and POSIX test suites" \
      "$C_ENGINE_HTML_LOG" python3 "$C_ENGINE_GENERATOR" \
      --repo-root "$REPO_ROOT" \
      --ocaml-layout \
      --runner "$C_ENGINE_RUNNER" \
      --wasm "$C_ENGINE_WASM" \
      --count \
      --output "$C_ENGINE_OCAML_LAYOUT_HTML"; then
    show_message "C-engine browser dashboard failed" \
      "HTML generation failed.\n\nLog: $C_ENGINE_HTML_LOG"
    return 1
  fi

  printf 'Completed: %s\n' "$(date --iso-8601=seconds)" >>"$C_ENGINE_HTML_LOG"
  show_message "C-engine browser dashboard generated" \
    "A self-contained C-engine dashboard was generated with the same groups as the OCaml-Wasm dashboard, including signaling/POSIX and libc tests.\n\nOutput: $C_ENGINE_OCAML_LAYOUT_HTML\nLog: $C_ENGINE_HTML_LOG"
}

generate_c_engine_bash_html() {
  if ! have_command cc || ! have_command clang || ! have_command make ||
      ! have_command flex || ! have_command bison || ! have_command python3 ||
      ! have_command node ||
      ! have_command wasm-as || ! have_command wasm-merge || ! have_command wasm-dis ||
      ! wasm_ld_is_usable; then
    show_message "C-engine Bash" \
      "cc, clang, make, flex, bison, wasm-ld, wasm-as, wasm-merge, wasm-dis, Node.js, and Python 3 are required."
    return 1
  fi
  if [[ ! -f "$REPO_ROOT/examples/bash.wat" || ! -f "$BASH_RUNTIME_BUILDER" ||
        ! -f "$C_ENGINE_BASH_GENERATOR" ||
        ! -f "$C_ENGINE_BASH_BROWSER_TEST" ]]; then
    show_message "C-engine Bash" \
      "Bash source or a generation tool is missing."
    return 1
  fi

  mkdir -p "$ENGINE_BUILD" "$HTML_BUILD"
  : >"$C_ENGINE_BASH_LOG"
  {
    printf 'WASTE C-engine Bash static HTML generation\n'
    printf 'Started: %s\n' "$(date --iso-8601=seconds)"
    printf 'Output: %s\n\n' "$C_ENGINE_BASH_HTML"
  } >>"$C_ENGINE_BASH_LOG"

  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    whiptail --title "C-engine Bash" --infobox \
      "Building the C engine and relinking Bash, then generating the static page...\n\nLog: $C_ENGINE_BASH_LOG" 9 84
  else
    printf 'Generating self-contained C-engine Bash page...\n'
  fi

  # Build the C engine Wasm (native yield/resume, no asyncify)
  if ! run_logged_step "Build browser C engine" "$C_ENGINE_BASH_LOG" \
      make -C "$REPO_ROOT/src/html-rt" BUILD_DIR="$HTML_BUILD" \
      ENGINE_BUILD_DIR="$ENGINE_BUILD" \
      wast-browser; then
    show_message "C-engine Bash failed" \
      "The C engine build failed.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  # Build the bash-runtime.wast (interactive mode for terminal I/O)
  if ! run_logged_step "Relink the interactive Bash runtime" \
      "$C_ENGINE_BASH_LOG" python3 "$BASH_RUNTIME_BUILDER" \
      --repo-root "$REPO_ROOT" \
      --interactive --output "$C_ENGINE_BASH_RUNTIME_WAST"; then
    show_message "C-engine Bash failed" \
      "Could not build the bash runtime.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  # Generate the HTML page
  if ! run_logged_step "Generate the self-contained C-engine Bash page" \
      "$C_ENGINE_BASH_LOG" python3 "$C_ENGINE_BASH_GENERATOR" \
      --repo-root "$REPO_ROOT" \
      --wasm "$C_ENGINE_WASM" \
      --launch "$C_ENGINE_BASH_RUNTIME_WAST" \
      --output "$C_ENGINE_BASH_HTML"; then
    show_message "C-engine Bash generation failed" \
      "Could not generate the static page.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  if ! run_logged_step "Run the C-engine Bash browser smoke test" \
      "$C_ENGINE_BASH_LOG" node "$C_ENGINE_BASH_BROWSER_TEST" \
      "$C_ENGINE_BASH_HTML"; then
    show_message "C-engine Bash browser test failed" \
      "The generated worker did not survive prompt, delayed input, command execution, and exit.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  printf 'Completed: %s\n' "$(date --iso-8601=seconds)" >>"$C_ENGINE_BASH_LOG"
  show_message "C-engine Bash generated" \
    "The static page embeds the C engine, shared runtime, waste-libc, and Bash. It can be opened directly with file:// and requires no server.\n\nOutput: $C_ENGINE_BASH_HTML\nLog: $C_ENGINE_BASH_LOG"
}

generate_c_engine_core_tests() {
  if ! have_command cc || ! have_command make || ! have_command flex ||
      ! have_command bison || ! have_command python3 || ! have_command node; then
    printf 'error: cc, make, flex, bison, python3, and node are required for C engine tests\n' >>"$TEST_LOG"
    return 1
  fi
  if [[ ! -d "$C_ENGINE_CORE_TESTS" ]]; then
    printf 'error: core tests not found at %s (run: git submodule update --init)\n' \
      "$C_ENGINE_CORE_TESTS" >>"$TEST_LOG"
    return 1
  fi
  printf '\n== C engine browser tests (official core WAST) ==\n' >>"$TEST_LOG"
  run_logged_step "Build native C engine" "$TEST_LOG" \
    make -C "$REPO_ROOT/src/cli-rt" BUILD_DIR="$CLI_BUILD" \
    ENGINE_BUILD_DIR="$ENGINE_BUILD" \
    wast-native || return 1
  run_logged_step "Build browser C engine" "$TEST_LOG" \
    make -C "$REPO_ROOT/src/html-rt" BUILD_DIR="$HTML_BUILD" \
    ENGINE_BUILD_DIR="$ENGINE_BUILD" \
    wast-browser || return 1
  run_logged_step "Generate core WAST browser dashboard" "$TEST_LOG" \
    python3 "$C_ENGINE_GENERATOR" --runner "$C_ENGINE_RUNNER" \
    --wasm "$C_ENGINE_WASM" --tests "$C_ENGINE_CORE_TESTS" \
    --output "$C_ENGINE_CORE_HTML" || return 1
  run_logged_step "Exercise core WAST browser dashboard" "$TEST_LOG" \
    node "$C_ENGINE_BROWSER_TEST" "$C_ENGINE_CORE_HTML"
}

run_test_group() {
  local group="$1"
  if [[ "$group" != spec && "$group" != tail && "$group" != tail-smoke &&
        "$group" != isolation && "$group" != c-engine ]] && ! have_command node; then
    show_message "Runtime tests" "Node.js is required to run the runtime test suites."
    return 1
  fi
  if [[ "$group" == spec || "$group" == tail || "$group" == tail-smoke ||
        "$group" == isolation || "$group" == posix || "$group" == libc ||
        "$group" == bash || "$group" == all ]] &&
      { ! have_command make || ! have_command opam || ! have_command python3; }; then
    show_message "Runtime tests" \
      "make, opam, and Python 3 are required to build and run the OCaml interpreter suite."
    return 1
  fi

  : >"$TEST_LOG"
  {
    printf 'WASTE runtime tests\n'
    printf 'Group: %s\n' "$group"
    printf 'Started: %s\n\n' "$(date --iso-8601=seconds)"
  } >>"$TEST_LOG"

  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    whiptail --title "Runtime tests" --infobox \
      "Running $group tests...\n\nLog: $TEST_LOG" 9 78
  else
    printf 'Running %s tests; log: %s\n' "$group" "$TEST_LOG"
  fi

  local status=0
  case "$group" in
    spec|tail|tail-smoke|isolation)
      run_logged_step "Build patched native OCaml interpreter" "$TEST_LOG" \
        build_ocaml_native || status=1
      ;;
    posix|libc|bash)
      run_logged_step "Build patched OCaml-to-Wasm interpreter" "$TEST_LOG" \
        build_ocaml_wasm || status=1
      ;;
    all)
      run_logged_step "Build patched OCaml-to-Wasm interpreter" "$TEST_LOG" \
        build_ocaml_wasm || status=1
      run_logged_step "Build patched native OCaml interpreter" "$TEST_LOG" \
        build_ocaml_native || status=1
      ;;
  esac
  if [[ "$group" == libc || "$group" == all ]]; then
    run_logged_step "Build guest libc and fixtures" "$TEST_LOG" \
      build_waste_libc true || status=1
  fi
  if ((status != 0)); then
    printf '\nBuild failed; final patch state: %s\n' \
      "$(i31_patch_status)" >>"$TEST_LOG"
    show_message "Runtime tests failed" \
      "An interpreter or generated test prerequisite failed. The Makefile attempted to restore the spec submodule.\n\nPatch status: $(i31_patch_status)\nLog: $TEST_LOG"
    return 1
  fi

  case "$group" in
    spec)
      run_official_core_tests || status=1
      ;;
    tail)
      run_official_tail_call_tests || status=1
      ;;
    tail-smoke)
      run_tail_call_smoke || status=1
      ;;
    isolation)
      run_sandbox_isolation_tests || status=1
      ;;
    c-engine)
      generate_c_engine_tests || status=1
      ;;
    c-engine-core)
      generate_c_engine_core_tests || status=1
      ;;
    posix)
      run_logged_test "POSIX control (sequential)" node "$REPO_ROOT/tests/diy-posix-test/posix-control-runtime.cjs" || status=1
      run_logged_test "POSIX control (threaded)" node "$REPO_ROOT/tests/diy-posix-test/posix-control-runtime.cjs" threaded || status=1
      run_logged_test "POSIX kernel (sequential)" node "$REPO_ROOT/tests/diy-posix-test/posix-kernel-runtime.cjs" || status=1
      run_logged_test "POSIX kernel (threaded)" node "$REPO_ROOT/tests/diy-posix-test/posix-kernel-runtime.cjs" threaded || status=1
      ;;
    libc)
      run_logged_test "libc (sequential)" node "$REPO_ROOT/tests/libc-test/libc-runtime.cjs" || status=1
      run_logged_test "libc (threaded)" node "$REPO_ROOT/tests/libc-test/libc-runtime.cjs" threaded || status=1
      run_logged_test "allocator (native)" node "$REPO_ROOT/tests/libc-test/allocator-native.cjs" || status=1
      ;;
    bash)
      run_logged_test "Bash interactive smoke test" node "$REPO_ROOT/tests/bash-interactive-runtime.cjs" || status=1
      ;;
    all)
      generate_c_engine_tests || status=1
      run_official_core_tests || status=1
      run_sandbox_isolation_tests || status=1
      run_logged_test "POSIX control (sequential)" node "$REPO_ROOT/tests/diy-posix-test/posix-control-runtime.cjs" || status=1
      run_logged_test "POSIX control (threaded)" node "$REPO_ROOT/tests/diy-posix-test/posix-control-runtime.cjs" threaded || status=1
      run_logged_test "POSIX kernel (sequential)" node "$REPO_ROOT/tests/diy-posix-test/posix-kernel-runtime.cjs" || status=1
      run_logged_test "POSIX kernel (threaded)" node "$REPO_ROOT/tests/diy-posix-test/posix-kernel-runtime.cjs" threaded || status=1
      run_logged_test "libc (sequential)" node "$REPO_ROOT/tests/libc-test/libc-runtime.cjs" || status=1
      run_logged_test "libc (threaded)" node "$REPO_ROOT/tests/libc-test/libc-runtime.cjs" threaded || status=1
      run_logged_test "allocator (native)" node "$REPO_ROOT/tests/libc-test/allocator-native.cjs" || status=1
      run_logged_test "Bash interactive smoke test" node "$REPO_ROOT/tests/bash-interactive-runtime.cjs" || status=1
      ;;
    *) return 2 ;;
  esac

  printf '\nFinished: %s\nExit status: %d\n' \
    "$(date --iso-8601=seconds)" "$status" >>"$TEST_LOG"
  if ((status == 0)); then
    show_message "Runtime tests passed" "$group tests completed successfully.\n\nLog: $TEST_LOG"
  else
    show_message "Runtime tests failed" "$group tests exited with status $status.\n\nLog: $TEST_LOG"
  fi
  return "$status"
}

test_suite_menu() {
  while true; do
    local choice
    choice="$(whiptail --title "Runtime test suites" --menu \
      "Tests run locally against the native, sequential, and CPS interpreters." \
      28 88 12 \
      all "Run official core, DIY POSIX, libc, and Bash suites" \
      c-engine "Build C engine and run relaxed-SIMD spec tests" \
      c-engine-core "Generate/run C engine official core WAST dashboard" \
      spec "Run the official WebAssembly core suite" \
      isolation "Run scheduled test-sandbox isolation regressions" \
      tail "Run the three official tail-call tests" \
      tail-smoke "Run a short tail-call performance smoke test" \
      posix "Run DIY POSIX control and kernel suites" \
      libc "Run waste-libc and native allocator suites" \
      bash "Run the interactive Bash smoke test" \
      log "Show the last test log" \
      back "Return to the main menu" 3>&1 1>&2 2>&3)" || return 0
    case "$choice" in
      all|c-engine|c-engine-core|spec|isolation|tail|tail-smoke|posix|libc|bash) run_test_group "$choice" || true ;;
      log)
        if [[ -s "$TEST_LOG" ]]; then
          whiptail --title "Last test log" --textbox "$TEST_LOG" 28 100
        else
          show_message "Last test log" "No test log exists yet."
        fi ;;
      back) return 0 ;;
    esac
  done
}

dependency_menu() {
  while true; do
    check_dependencies && return 0

    if ! have_command whiptail || [[ ! -t 0 || ! -t 1 ]]; then
      printf '%s\n\nSuggested system command:\n%s\n' "$STATUS_TEXT" "$(system_install_command)"
      if confirm "Try to install the missing dependencies now?"; then
        install_missing_dependencies && return 0
      fi
      return 1
    fi

    local choice
    choice="$(whiptail --title "OCaml Wasm dependencies" --menu \
      "$STATUS_TEXT" 24 82 4 \
      install "Install or repair dependencies" \
      commands "Show installation commands" \
      recheck "Check again" \
      quit "Exit" 3>&1 1>&2 2>&3)" || return 1

    case "$choice" in
      install) install_missing_dependencies || true ;;
      commands)
        show_message "Installation commands" \
          "System:\n$(system_install_command)\n\nopam:\nopam init --bare --yes\nopam switch create $SWITCH_NAME $OCAML_VERSION --yes\nopam install --switch=$SWITCH_NAME ${OPAM_PACKAGES[*]}" ;;
      recheck) ;;
      quit) return 1 ;;
    esac
  done
}

main_menu() {
  while true; do
    local choice
    choice="$(whiptail --title "WASTE" --menu \
      "WebAssembly Threading Environment" 28 78 16 \
      -- \
      sync    "Git pull/rebase with autostash" \
      ---     "── Engine ──────────────────────────────────" \
      cli-compile "Build engine with cli runtime" \
      cli-test    "Run the full test suite in the cli runtime" \
      html-test   "Compile engine into static HTML for browser tests" \
      html-bash   "Compile engine and example bash into static HTML" \
      ----    "── OCaml ───────────────────────────────────" \
      ocaml-compile "Compile the OCaml interpreter to Wasm" \
      ocaml-test    "Run the full test suite with OCaml interpreter" \
      ocaml-html    "Generate embedded browser test dashboard" \
      ocaml-bash    "Generate self-contained WASTE Bash page" \
      -----   "────────────────────────────────────────────" \
      quit    "Exit" 3>&1 1>&2 2>&3)" || return 0

    case "$choice" in
      sync) safe_repository_update || true ;;
      cli-compile) compile_cli_engine || true ;;
      cli-test) run_cli_tests || true ;;
      html-test) generate_c_engine_dashboard_html || true ;;
      html-bash) generate_c_engine_bash_html || true ;;
      ocaml-compile) compile_interpreter || true ;;
      ocaml-test) test_suite_menu ;;
      ocaml-html) generate_browser_test_html || true ;;
      ocaml-bash) generate_bash_html || true ;;
      quit) return 0 ;;
    esac
  done
}

main() {
  local action="wizard"
  if (($# > 1)); then
    usage >&2
    return 2
  fi
  if (($# == 1)); then
    action="$1"
  fi

  case "$action" in
    --help|-h) usage ;;
    --check)
      if check_dependencies; then
        printf '%s\n' "$STATUS_TEXT"
        return 0
      fi
      printf '%s\n\nSuggested system command:\n%s\n' "$STATUS_TEXT" "$(system_install_command)"
      return 1 ;;
    --install-deps) install_missing_dependencies ;;
    --sync|--update) safe_repository_update ;;
    --cli-compile) compile_cli_engine ;;
    --cli-test) run_cli_tests ;;
    --compile) compile_interpreter ;;
    --build-libc) build_waste_libc ;;
    --generate-html) generate_browser_test_html ;;
    --generate-bash-html) generate_bash_html ;;
    --c-engine-tests)
      : >"$TEST_LOG"
      c_engine_status=0
      generate_c_engine_tests || c_engine_status=$?
      printf '\nFinished: %s\n' "$(date --iso-8601=seconds)" >>"$TEST_LOG"
      return "$c_engine_status" ;;
    --c-engine-html|--html-test) generate_c_engine_dashboard_html ;;
    --c-engine-bash-html|--html-bash) generate_c_engine_bash_html ;;
    --c-engine-core-tests)
      : >"$TEST_LOG"
      c_engine_status=0
      generate_c_engine_core_tests || c_engine_status=$?
      printf '\nFinished: %s\n' "$(date --iso-8601=seconds)" >>"$TEST_LOG"
      return "$c_engine_status" ;;
    --patch-status) i31_patch_status ;;
    --apply-i31) apply_i31_patch ;;
    --revert-i31) revert_i31_patch ;;
    wizard)
      dependency_menu || return 1
      main_menu ;;
    *)
      usage >&2
      return 2 ;;
  esac
}

main "$@"
