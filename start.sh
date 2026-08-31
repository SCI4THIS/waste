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
BUILD_ROOT="$REPO_ROOT/build/ocaml-wasm"
STAGING_DIR="$BUILD_ROOT/staging/interpreter"
DIST_DIR="$BUILD_ROOT/dist"
THREADED_DIST_DIR="$BUILD_ROOT/dist-threaded"
LOG_FILE="$REPO_ROOT/build.log"
UPDATE_LOG="$REPO_ROOT/update.log"
BROWSER_TEST_GENERATOR="$REPO_ROOT/tools/generate-browser-tests.py"
BROWSER_TEST_HTML="$BUILD_ROOT/browser-tests.html"
BROWSER_THREADED_HTML="$BUILD_ROOT/browser-tests-threaded.html"
HTML_LOG="$REPO_ROOT/html.log"
THREADED_HTML_LOG="$REPO_ROOT/threaded-html.log"

SWITCH_NAME="${WASTE_OCAML_SWITCH:-waste-wasm}"
OCAML_VERSION="${WASTE_OCAML_VERSION:-5.3.0}"
OPAM_PACKAGES=(dune menhir wasm_of_ocaml-compiler js_of_ocaml js_of_ocaml-ppx)

declare -a MISSING_SYSTEM=()
declare -a MISSING_OPAM=()
STATUS_TEXT=""

usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTION]

Dependency-first wizard for compiling the official OCaml WebAssembly
interpreter to WebAssembly.

  --check          print dependency status and exit
  --install-deps   interactively install missing dependencies
  --compile        compile without opening the main menu
  --generate-html  generate the embedded browser test dashboard
  --generate-threaded-html
                   generate the cooperative threaded browser dashboard
  --patch-status   show the Wasm32 compatibility patch status
  --apply-i31      apply the Wasm32 patch (legacy option name)
  --revert-i31     revert the Wasm32 patch (legacy option name)
  --update         safely pull, update submodules, and restore the patch
  --help           show this help

Environment overrides:
  WASTE_OCAML_SWITCH   opam switch name (default: $SWITCH_NAME)
  WASTE_OCAML_VERSION  OCaml version for a new switch (default: $OCAML_VERSION)
  WASTE_INSTRUCTION_QUANTUM
                       threaded scheduler quantum (default: 10000)
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
  for command_name in git cc make opam bwrap whiptail; do
    if ! have_command "$command_name"; then
      MISSING_SYSTEM+=("$command_name")
    fi
  done
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
    pacman) printf '%s\n' "sudo pacman -S --needed git opam bubblewrap base-devel binaryen libnewt" ;;
    apt) printf '%s\n' "sudo apt-get install git opam bubblewrap build-essential binaryen whiptail" ;;
    dnf) printf '%s\n' "sudo dnf install git opam bubblewrap gcc make binaryen newt" ;;
    zypper) printf '%s\n' "sudo zypper install git opam bubblewrap gcc make binaryen newt" ;;
    *) printf '%s\n' "Install: opam, a C compiler, make, Binaryen 119+, and whiptail" ;;
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

prepare_overlay() {
  rm -rf -- "$STAGING_DIR"
  mkdir -p -- "$STAGING_DIR" "$DIST_DIR"
  cp -a -- "$INTERPRETER_DIR/." "$STAGING_DIR/"
  rm -rf -- "$STAGING_DIR/_build"

  sed -i 's/^(lang dune 2\.9)$/\(lang dune 3.17\)/' "$STAGING_DIR/dune-project"
  sed -i 's/(modules :standard \\ main wasm wast smallint)/(modules :standard \\ main wasm_cli wast smallint)/' "$STAGING_DIR/dune"
  sed -i '/^(executable$/ { n; s/^  (public_name wasm)$/  (name wasm_cli)\n  (public_name wasm)/; }' "$STAGING_DIR/dune"
  sed -i '0,/^  (modules wasm)$/s//  (modules wasm_cli)\n  (modes exe wasm)/' "$STAGING_DIR/dune"
  sed -i 's/(targets wasm\.ml)/(targets wasm_cli.ml)/' "$STAGING_DIR/dune"
  sed -i 's/wasm\.ml))$/wasm_cli.ml))/' "$STAGING_DIR/dune"

  grep -Fqx '(lang dune 3.17)' "$STAGING_DIR/dune-project" &&
    grep -Fqx '  (modes exe wasm)' "$STAGING_DIR/dune" &&
    grep -Fqx '  (name wasm_cli)' "$STAGING_DIR/dune"
}

enable_threaded_overlay() {
  sed -i '0,/^  (libraries wasm)$/s//  (libraries wasm)\n  (wasm_of_ocaml (flags (:standard --effects cps)))/' "$STAGING_DIR/dune"
  grep -Fqx '  (wasm_of_ocaml (flags (:standard --effects cps)))' "$STAGING_DIR/dune"
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

  if ! prepare_overlay >>"$LOG_FILE" 2>&1; then
    show_message "Build failed" "Could not prepare the Dune overlay.\n\nLog: $LOG_FILE"
    return 1
  fi

  {
    printf 'Overlay: %s\n' "$STAGING_DIR"
    printf 'Sequential command: opam exec --switch=%s -- dune build --profile release ./wasm_cli.bc.wasm.js\n\n' "$SWITCH_NAME"
  } >>"$LOG_FILE"

  if ! (
    cd -- "$STAGING_DIR"
    opam exec --switch="$SWITCH_NAME" -- \
      dune build --profile release ./wasm_cli.bc.wasm.js
  ) >>"$LOG_FILE" 2>&1; then
    show_message "Build failed" "Dune or wasm_of_ocaml failed.\n\nLog: $LOG_FILE"
    return 1
  fi

  if [[ ! -f "$STAGING_DIR/_build/default/wasm_cli.bc.wasm.js" ||
        ! -d "$STAGING_DIR/_build/default/wasm_cli.bc.wasm.assets" ]]; then
    show_message "Build failed" "Dune reported success but did not produce the expected Wasm files.\n\nLog: $LOG_FILE"
    return 1
  fi

  rm -rf -- "$DIST_DIR"
  mkdir -p -- "$DIST_DIR"
  if ! cp -- "$STAGING_DIR/_build/default/wasm_cli.bc.wasm.js" "$DIST_DIR/" ||
     ! cp -a -- "$STAGING_DIR/_build/default/wasm_cli.bc.wasm.assets" "$DIST_DIR/"; then
    show_message "Build failed" "The Wasm files were built but could not be copied to the output directory.\n\nLog: $LOG_FILE"
    return 1
  fi

  {
    printf '\nEnabling CPS continuations for the cooperative threaded build.\n'
    printf 'Threaded command: opam exec --switch=%s -- dune build --profile release ./wasm_cli.bc.wasm.js\n\n' "$SWITCH_NAME"
  } >>"$LOG_FILE"

  if ! enable_threaded_overlay >>"$LOG_FILE" 2>&1 || ! (
    cd -- "$STAGING_DIR"
    opam exec --switch="$SWITCH_NAME" -- \
      dune build --profile release ./wasm_cli.bc.wasm.js
  ) >>"$LOG_FILE" 2>&1; then
    show_message "Threaded build failed" "The sequential build succeeded, but the CPS threaded build failed.\n\nLog: $LOG_FILE"
    return 1
  fi

  rm -rf -- "$THREADED_DIST_DIR"
  mkdir -p -- "$THREADED_DIST_DIR"
  if ! cp -- "$STAGING_DIR/_build/default/wasm_cli.bc.wasm.js" "$THREADED_DIST_DIR/" ||
     ! cp -a -- "$STAGING_DIR/_build/default/wasm_cli.bc.wasm.assets" "$THREADED_DIST_DIR/"; then
    show_message "Threaded build failed" "The CPS Wasm files were built but could not be copied to the threaded output directory.\n\nLog: $LOG_FILE"
    return 1
  fi

  {
    printf '\nCompleted: %s\n' "$(date --iso-8601=seconds)"
    printf 'Loader: %s\n' "$DIST_DIR/wasm_cli.bc.wasm.js"
    printf 'Assets: %s\n' "$DIST_DIR/wasm_cli.bc.wasm.assets"
    printf 'Threaded loader: %s\n' "$THREADED_DIST_DIR/wasm_cli.bc.wasm.js"
    printf 'Threaded assets: %s\n' "$THREADED_DIST_DIR/wasm_cli.bc.wasm.assets"
  } >>"$LOG_FILE"

  show_message "Build complete" \
    "The spec interpreter was compiled using Wasm32 patch state: $compile_patch_state. The compilation itself did not modify the submodule.\n\nSequential: $DIST_DIR/wasm_cli.bc.wasm.js\nThreaded CPS: $THREADED_DIST_DIR/wasm_cli.bc.wasm.js\nLog: $LOG_FILE"
}

generate_browser_test_html() {
  local mode="${1:-sequential}"
  local quantum="${2:-${WASTE_INSTRUCTION_QUANTUM:-10000}}"
  local output="$BROWSER_TEST_HTML"
  local html_log="$HTML_LOG"
  local description="sequential"
  local loader_dist="$DIST_DIR"
  if [[ "$mode" == "threaded" ]]; then
    output="$BROWSER_THREADED_HTML"
    html_log="$THREADED_HTML_LOG"
    description="cooperative threaded"
    loader_dist="$THREADED_DIST_DIR"
  fi
  if [[ ! "$quantum" =~ ^[1-9][0-9]*$ ]]; then
    show_message "Browser test dashboard" "Instruction quantum must be a positive integer."
    return 1
  fi

  : >"$html_log"
  {
    printf 'WASTE %s browser test dashboard generation\n' "$description"
    printf 'Started: %s\n' "$(date --iso-8601=seconds)"
    printf 'Output: %s\n' "$output"
    printf 'Instruction quantum: %s\n\n' "$quantum"
  } >>"$html_log"

  if ! have_command python3; then
    printf 'error: Python 3 is not installed\n' >>"$html_log"
    show_message "Browser test dashboard" "Python 3 is required to generate the HTML.\n\nLog: $html_log"
    return 1
  fi
  if [[ ! -f "$loader_dist/wasm_cli.bc.wasm.js" ]]; then
    printf 'error: compiled Wasm loader is missing\n' >>"$html_log"
    show_message "Browser test dashboard" "Compile the OCaml interpreter to Wasm first.\n\nLog: $html_log"
    return 1
  fi
  if [[ ! -f "$BROWSER_TEST_GENERATOR" ]]; then
    printf 'error: generator is missing: %s\n' "$BROWSER_TEST_GENERATOR" >>"$html_log"
    show_message "Browser test dashboard" "The HTML generator is missing.\n\nLog: $html_log"
    return 1
  fi

  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    whiptail --title "Browser test dashboard" --infobox \
      "Embedding the compiled OCaml Wasm and all specification tests...\n\nLog: $html_log" 9 78
  else
    printf 'Generating embedded browser test dashboard...\n'
  fi

  if ! python3 "$BROWSER_TEST_GENERATOR" --repo-root "$REPO_ROOT" \
      --mode "$mode" --quantum "$quantum" --output "$output" >>"$html_log" 2>&1; then
    show_message "Browser test dashboard failed" "HTML generation failed.\n\nLog: $html_log"
    return 1
  fi

  show_message "Browser test dashboard generated" \
    "A self-contained $description HTML dashboard was generated with the OCaml Wasm and all .wast tests embedded.\n\nOutput: $output\nLog: $html_log"
}

generate_threaded_browser_test_html() {
  local quantum="${WASTE_INSTRUCTION_QUANTUM:-10000}"
  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    quantum="$(whiptail --title "Cooperative scheduler" --inputbox \
      "Interpreter steps per test before switching:" 9 64 "$quantum" \
      3>&1 1>&2 2>&3)" || return 1
  fi
  generate_browser_test_html threaded "$quantum"
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
    local patch_state
    local choice
    patch_state="$(i31_patch_status)"
    choice="$(whiptail --title "OCaml to WebAssembly" --menu \
      "Switch: $SWITCH_NAME    Spec: submodules/wasm-spec" 25 92 8 \
      compile "Compile the OCaml interpreter to Wasm" \
      html "Generate embedded browser test dashboard" \
      threaded "Generate cooperative threaded browser dashboard" \
      patch "Manage Wasm32 compatibility patch [$patch_state]" \
      update "Safe pull/rebase and submodule update" \
      status "Show dependency status" \
      log "Show the last build log" \
      quit "Exit" 3>&1 1>&2 2>&3)" || return 0

    case "$choice" in
      compile) compile_interpreter || true ;;
      html) generate_browser_test_html || true ;;
      threaded) generate_threaded_browser_test_html || true ;;
      patch) i31_patch_menu ;;
      update) safe_repository_update || true ;;
      status)
        check_dependencies || true
        show_message "Dependency status" "$STATUS_TEXT" ;;
      log)
        if [[ -s "$LOG_FILE" ]]; then
          whiptail --title "Last build log" --textbox "$LOG_FILE" 28 100
        else
          show_message "Last build log" "No build log exists yet."
        fi ;;
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
    --compile) compile_interpreter ;;
    --generate-html) generate_browser_test_html ;;
    --generate-threaded-html) generate_threaded_browser_test_html ;;
    --patch-status) i31_patch_status ;;
    --apply-i31) apply_i31_patch ;;
    --revert-i31) revert_i31_patch ;;
    --update) safe_repository_update ;;
    wizard)
      dependency_menu || return 1
      main_menu ;;
    *)
      usage >&2
      return 2 ;;
  esac
}

main "$@"
