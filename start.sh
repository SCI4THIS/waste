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
ENGINE_BUILD="$REPO_ROOT/build/engine"
CLI_BUILD="$REPO_ROOT/build/cli-rt"
HTML_BUILD="$REPO_ROOT/build/html-rt"
LOG_DIR="$ENGINE_BUILD/logs"
UPDATE_LOG="$LOG_DIR/update.log"
BASH_RUNTIME_BUILDER="$REPO_ROOT/src/html-rt/tools/build-bash-runtime.py"
LIBC_OUTPUT="$HTML_BUILD/waste-libc/waste-libc.wasm"
LIBC_LOG="$LOG_DIR/libc-build.log"
TEST_LOG="$LOG_DIR/test.log"
C_ENGINE_RUNNER="$CLI_BUILD/waste-cli"
C_ENGINE_WASM="$HTML_BUILD/waste-wast.wasm"
C_ENGINE_GENERATOR="$REPO_ROOT/src/html-rt/tools/generate-c-engine-tests.py"
C_ENGINE_BUILD_LOG="$LOG_DIR/c-engine-build.log"
C_ENGINE_CORE_TESTS="$REPO_ROOT/submodules/wasm-spec/test/core"
C_ENGINE_RELAXED_SIMD_TESTS="$REPO_ROOT/submodules/wasm-spec/test/core/relaxed-simd"
C_ENGINE_MEMORY64_TESTS="$REPO_ROOT/submodules/wasm-spec/test/core/memory64"
C_ENGINE_BULK_MEMORY_TESTS="$REPO_ROOT/submodules/wasm-spec/test/core/bulk-memory"
C_ENGINE_DIY_POSIX_TESTS="$REPO_ROOT/tests/diy-posix-test"
C_ENGINE_BROWSER_TEST="$REPO_ROOT/tests/c-engine-browser-runtime.cjs"
C_ENGINE_BASH_GENERATOR="$REPO_ROOT/src/html-rt/tools/generate-c-engine-bash-html.py"
C_ENGINE_BASH_BROWSER_TEST="$REPO_ROOT/tests/c-engine-bash-browser-runtime.cjs"
C_ENGINE_TERMINAL_MODEL_TEST="$REPO_ROOT/tests/c-engine-terminal-model.cjs"
C_ENGINE_BASH_RUNTIME_WAST="$HTML_BUILD/bash-runtime.wast"
C_ENGINE_BASH_HTML="$HTML_BUILD/bash.html"
C_ENGINE_BASH_LOG="$LOG_DIR/c-engine-bash.log"
C_ENGINE_BUILD_SH="$REPO_ROOT/src/html-rt/tools/build.sh"
C_ENGINE_STAGING_TESTS="$HTML_BUILD/tests"
C_ENGINE_STAGING_BASH="$HTML_BUILD/bash"

mkdir -p "$LOG_DIR"

declare -a MISSING_SYSTEM=()
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
  --html-bash      build the self-contained C-engine Bash page
  --html-check     run focused worker and terminal-model checks (Node, no Chromium)
  --html-browser-full
                   run full offline-browser verification (requires Chromium)
  --build-aux      build all auxiliary Wasm utilities (coreutils, rogue, ncurses, ldd)
                   and install to repo

Legacy / advanced:
  --build-libc     build libc.so.wasm and the static libc test profile
  --c-engine-tests build C engine and run relaxed-SIMD spec tests
  --c-engine-bash-html
                   alias for --html-bash
  --c-engine-core-tests
                   run official-core WAST tests through the browser worker harness
  --ocaml-reference
                   stage and run the unpatched OCaml reference interpreter
  --update         alias for --sync
  --help           show this help

Environment overrides:
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

check_dependencies() {
  MISSING_SYSTEM=()

  local command_name
  for command_name in git cc clang make whiptail wasm-as wasm-merge wasm-dis flex bison; do
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

  local submodule_status="ready"
  if [[ ! -f "$INTERPRETER_DIR/dune-project" ]]; then
    submodule_status="empty"
  fi

  local system_status="all present"
  ((${#MISSING_SYSTEM[@]})) && system_status="missing: ${MISSING_SYSTEM[*]}"

  STATUS_TEXT="System tools: $system_status
./submodules/wasm-spec: $submodule_status"

  [[ "$submodule_status" == "ready" ]] &&
    ((${#MISSING_SYSTEM[@]} == 0))
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
    pacman) printf '%s\n' "sudo pacman -S --needed git base-devel clang lld binaryen libnewt" ;;
    apt) printf '%s\n' "sudo apt-get install git build-essential clang lld binaryen whiptail" ;;
    dnf) printf '%s\n' "sudo dnf install git gcc clang lld make binaryen newt" ;;
    zypper) printf '%s\n' "sudo zypper install git gcc clang lld make binaryen newt" ;;
    *) printf '%s\n' "Install: clang/lld, a C compiler, make, Binaryen 119+, and whiptail" ;;
  esac
}

confirm() {
  local prompt="$1"
  prompt="${prompt//\\n/$'\n'}"
  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    whiptail --title "WASTE setup" --yesno "$prompt" 12 76
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
    pacman) sudo pacman -S --needed git base-devel binaryen libnewt ;;
    apt) sudo apt-get install git build-essential binaryen whiptail ;;
    dnf) sudo dnf install git gcc make binaryen newt ;;
    zypper) sudo zypper install git gcc make binaryen newt ;;
  esac
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

safe_repository_update() {
  if ! confirm "Perform a repository update?\n\n1. git pull --rebase --autostash\n2. git submodule update --init --recursive\n\nNote: Git autostash does not include untracked files."; then
    return 1
  fi

  : >"$UPDATE_LOG"
  {
    printf 'WASTE repository update\n'
    printf 'Started: %s\n\n' "$(date --iso-8601=seconds)"
    printf '$ git pull --rebase --autostash\n'
  } >>"$UPDATE_LOG"

  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    whiptail --title "Repository update" --infobox \
      "Pulling and rebasing the main repository...\n\nLog: $UPDATE_LOG" 9 76
  fi

  if ! git -C "$REPO_ROOT" pull --rebase --autostash >>"$UPDATE_LOG" 2>&1; then
    show_message "Repository update failed" \
      "git pull failed.\n\nLog: $UPDATE_LOG"
    return 1
  fi

  printf '\n$ git submodule update --init --recursive\n' >>"$UPDATE_LOG"
  if ! git -C "$REPO_ROOT" submodule update --init --recursive >>"$UPDATE_LOG" 2>&1; then
    show_message "Repository update incomplete" \
      "The main repository updated, but the submodule update failed.\n\nLog: $UPDATE_LOG"
    return 1
  fi

  printf '\nCompleted: %s\n' "$(date --iso-8601=seconds)" >>"$UPDATE_LOG"
  show_message "Repository update complete" \
    "The repository and submodules are updated.\n\nLog: $UPDATE_LOG"
}

install_missing_dependencies() {
  check_dependencies || true
  if ((${#MISSING_SYSTEM[@]})); then
    install_system_dependencies || return 1
  fi
  initialize_submodule || return 1
  check_dependencies
}

build_waste_libc() {
  local quiet="${1:-false}"
  mkdir -p -- "$(dirname -- "$LIBC_OUTPUT")"
  : >"$LIBC_LOG"
  {
    printf 'WASTE guest libc build\n'
    printf 'Started: %s\n' "$(date --iso-8601=seconds)"
    printf 'Source: %s\n' "$REPO_ROOT/src/libc"
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
      ENGINE_BUILD_DIR="$ENGINE_BUILD" libc-shared waste-libc; then
    if [[ "$quiet" != true ]]; then
      show_message "Guest libc build failed" "Could not build waste-libc.wasm.\n\nLog: $LIBC_LOG"
    fi
    return 1
  fi
  printf 'Completed: %s\n' "$(date --iso-8601=seconds)" >>"$LIBC_LOG"
  if [[ "$quiet" != true ]]; then
    show_message "Guest libc build complete" \
      "The PIC shared library and static test fixtures were generated.\n\nShared library: $REPO_ROOT/build/libc-shared/libc.so.wasm\nTest profile: $LIBC_OUTPUT\nLog: $LIBC_LOG"
  fi
}

run_logged_test() {
  local label="$1"
  shift
  run_logged_step "$label" "$TEST_LOG" "$@"
}

compile_cli_engine() {
  if ! have_command cc || ! have_command make || ! have_command flex || ! have_command bison; then
    show_message "CLI engine compile" \
      "cc, make, flex, and bison are required."
    return 1
  fi
  mkdir -p "$ENGINE_BUILD" "$CLI_BUILD"
  : >"$C_ENGINE_BUILD_LOG"
  {
    printf 'CLI engine compile\n'
    printf 'Started: %s\n\n' "$(date --iso-8601=seconds)"
  } >>"$C_ENGINE_BUILD_LOG"

  if have_command whiptail && [[ -t 0 && -t 1 ]]; then
    whiptail --title "CLI engine compile" --infobox \
      "Building the C engine with the CLI runtime...\n\nLog: $C_ENGINE_BUILD_LOG" 9 78
  else
    printf 'Building the C engine with the CLI runtime...\n'
  fi

  if ! run_logged_step "Build native C engine" "$C_ENGINE_BUILD_LOG" \
      make -C "$REPO_ROOT/src/cli-rt" BUILD_DIR="$CLI_BUILD" \
      ENGINE_BUILD_DIR="$ENGINE_BUILD" \
      wast-native; then
    show_message "CLI engine compile failed" \
      "The build failed.\n\nLog: $C_ENGINE_BUILD_LOG"
    return 1
  fi
  printf 'Completed: %s\n' "$(date --iso-8601=seconds)" >>"$C_ENGINE_BUILD_LOG"
  show_message "CLI engine compile" \
    "The native WAST runner was built successfully.\n\nOutput: $C_ENGINE_RUNNER\nLog: $C_ENGINE_BUILD_LOG"
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
  run_logged_step "Generate relaxed-SIMD/POSIX test data" "$TEST_LOG" \
    python3 "$C_ENGINE_GENERATOR" \
    --runner "$C_ENGINE_RUNNER" \
    --wasm "$C_ENGINE_WASM" \
    --tests "$C_ENGINE_RELAXED_SIMD_TESTS" \
    --tests "$C_ENGINE_MEMORY64_TESTS" \
    --tests "$C_ENGINE_BULK_MEMORY_TESTS" \
    --tests "$C_ENGINE_DIY_POSIX_TESTS" \
    --output-dir "$C_ENGINE_STAGING_TESTS" || return 1
  run_logged_step "Exercise official WAST browser workers" "$TEST_LOG" \
    node "$C_ENGINE_BROWSER_TEST"
}

# ── Auxiliary utility build helpers ──────────────────────────────────────

AUX_UTILITIES=(true false pwd echo printf basename dirname cat wc ls date rogue libncurses ldd upload download)
AUX_STAGING="$REPO_ROOT/src/vfs"
AUX_LOG="$LOG_DIR/aux-build.log"

# Return the staged file path for a given utility name.
aux_staged_path() {
  local utility="$1"
  case "$utility" in
    libncurses) printf '%s' "$AUX_STAGING/lib/libncurses.so.wasm" ;;
    *)          printf '%s' "$AUX_STAGING/usr/bin/${utility}" ;;
  esac
}

build_single_aux() {
  local utility="$1"
  if ! have_command clang || ! have_command make || ! have_command python3 ||
      ! have_command wasm-as || ! have_command wasm-merge || ! have_command wasm-dis ||
      ! wasm_ld_is_usable; then
    show_message "Aux build" \
      "clang, make, wasm-ld, wasm-as, wasm-merge, wasm-dis, and Python 3 are required."
    return 1
  fi
  mkdir -p "$HTML_BUILD"
  : >"$AUX_LOG"

  case "$utility" in
    libncurses)
      if ! run_logged_step "Build ncurses shared library" \
          "$AUX_LOG" python3 "$REPO_ROOT/src/html-rt/tools/build-ncurses.py" \
          --repo-root "$REPO_ROOT" --output "$REPO_ROOT/build/ncurses"; then
        show_message "Aux build failed" \
          "Could not build libncurses.\n\nLog: $AUX_LOG"
        return 1
      fi
      local ncurses_out="$REPO_ROOT/build/ncurses/vfs/libncurses.so.wasm"
      if [[ ! -f "$ncurses_out" ]]; then
        show_message "Aux build failed" \
          "Linked artifact not found: $ncurses_out"
        return 1
      fi
      local review_text
      review_text=$(python3 "$REPO_ROOT/src/html-rt/tools/shared_libc.py" \
        --review-imports "$ncurses_out") || return 1
      local -a libc_reviews
      read -r -a libc_reviews <<< "$review_text"
      python3 "$REPO_ROOT/src/html-rt/tools/build-guest-sdk.py" --install \
        "${libc_reviews[@]}" --library "$ncurses_out" || return 1
      printf 'Installed %s → %s\n' "$ncurses_out" "$AUX_STAGING/libncurses.so.wasm"
      ;;
    rogue)
      if ! run_logged_step "Build rogue executable" \
          "$AUX_LOG" python3 "$REPO_ROOT/src/html-rt/tools/build-rogue.py" \
          --repo-root "$REPO_ROOT" --output "$REPO_ROOT/build/rogue"; then
        show_message "Aux build failed" \
          "Could not build rogue.\n\nLog: $AUX_LOG"
        return 1
      fi
      local rogue_out="$REPO_ROOT/build/rogue/vfs/rogue"
      if [[ ! -f "$rogue_out" ]]; then
        show_message "Aux build failed" \
          "Linked artifact not found: $rogue_out"
        return 1
      fi
      local review_text
      review_text=$(python3 "$REPO_ROOT/src/html-rt/tools/shared_libc.py" \
        --review-imports "$rogue_out") || return 1
      local -a libc_reviews
      read -r -a libc_reviews <<< "$review_text"
      python3 "$REPO_ROOT/src/html-rt/tools/vfs.py" install \
        "${libc_reviews[@]}" \
        --component "$utility" --source "$rogue_out" || return 1
      printf 'Installed %s → %s\n' "$rogue_out" "$AUX_STAGING/rogue.wasm"
      ;;
    ldd|upload|download)
      if ! run_logged_step "Build and install $utility utility" \
          "$AUX_LOG" make -C "$REPO_ROOT/src/aux" "install-$utility"; then
        show_message "Aux build failed" \
          "Could not build/install $utility.\n\nLog: $AUX_LOG"
        return 1
      fi
      local aux_out="$REPO_ROOT/build/aux/${utility}/${utility}.wasm"
      printf 'Installed %s → %s\n' "$aux_out" "$AUX_STAGING/usr/bin/$utility"
      ;;
    *)
      if ! run_logged_step "Build and audit coreutils $utility" \
          "$AUX_LOG" make -C "$REPO_ROOT/src/html-rt" \
          BUILD_DIR="$HTML_BUILD" "coreutils-${utility}-probe"; then
        show_message "Aux build failed" \
          "Could not build coreutils $utility.\n\nLog: $AUX_LOG"
        return 1
      fi
      local report_dir="$REPO_ROOT/build/coreutils/utility-probe"
      local linked="$report_dir/${utility}-linked.wasm"
      if [[ ! -f "$linked" ]]; then
        show_message "Aux build failed" \
          "Linked artifact not found: $linked"
        return 1
      fi
      local review_text
      review_text=$(python3 "$REPO_ROOT/src/html-rt/tools/shared_libc.py" \
        --review-imports "$linked") || return 1
      local -a libc_reviews
      read -r -a libc_reviews <<< "$review_text"
      python3 "$REPO_ROOT/src/html-rt/tools/vfs.py" install \
        "${libc_reviews[@]}" \
        --review-import waste_kernel:dlopen_v1:function \
        --review-import waste_kernel:dlsym_v1:function \
        --review-import waste_kernel:dlclose_v1:function \
        --component "$utility" --source "$linked" || return 1
      printf 'Installed %s → %s\n' "$linked" "$AUX_STAGING/${utility}.wasm"
      ;;
  esac
}

build_all_aux() {
  local failed=()
  for utility in "${AUX_UTILITIES[@]}"; do
    if ! build_single_aux "$utility"; then
      failed+=("$utility")
    fi
  done
  if ((${#failed[@]} > 0)); then
    show_message "Aux build" \
      "Failed utilities: ${failed[*]}"
    return 1
  fi
  show_message "Aux build" \
    "All ${#AUX_UTILITIES[@]} utilities built and installed:\n${AUX_UTILITIES[*]}"
}

aux_menu() {
  if ! have_command whiptail || [[ ! -t 0 || ! -t 1 ]]; then
    build_all_aux
    return
  fi
  while true; do
    local items=("all" "Build all aux (${AUX_UTILITIES[*]})")
    for utility in "${AUX_UTILITIES[@]}"; do
      local status="not built"
      local staged
      staged="$(aux_staged_path "$utility")"
      if [[ -f "$staged" ]]; then
        status="$(stat -c '%Y' "$staged" 2>/dev/null | xargs -I{} date -d @{} '+%Y-%m-%d %H:%M' 2>/dev/null || echo 'built')"
      fi
      items+=("$utility" "Build $utility ($status)")
    done
    items+=("back" "Return to main menu")

    local choice
    choice="$(whiptail --title "Aux" --menu \
      "Build auxiliary Wasm utilities" 22 70 14 \
      -- "${items[@]}" 3>&1 1>&2 2>&3)" || return 0

    case "$choice" in
      all)  build_all_aux || true ;;
      back) return 0 ;;
      *)    build_single_aux "$choice" || true ;;
    esac
  done
}

generate_c_engine_bash_html() {
  if ! have_command cc || ! have_command clang || ! have_command make ||
      ! have_command flex || ! have_command bison || ! have_command python3 ||
      ! have_command wasm-as || ! have_command wasm-merge || ! have_command wasm-dis ||
      ! wasm_ld_is_usable; then
    show_message "C-engine Bash" \
      "cc, clang, make, flex, bison, wasm-ld, wasm-as, wasm-merge, wasm-dis, and Python 3 are required."
    return 1
  fi
  if [[ ! -f "$REPO_ROOT/examples/bash.wat" || ! -f "$BASH_RUNTIME_BUILDER" ||
        ! -f "$C_ENGINE_BASH_GENERATOR" ]]; then
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

  if ! run_logged_step "Build and install shared guest libc" \
      "$C_ENGINE_BASH_LOG" make -C "$REPO_ROOT/src/libc" install; then
    show_message "C-engine Bash failed" \
      "Could not build and install libc.so.wasm.\n\nLog: $C_ENGINE_BASH_LOG"
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

  # Verify pre-built auxiliary Wasm files are present.
  local aux_missing=()
  for util_name in "${AUX_UTILITIES[@]}"; do
    local staged
    staged="$(aux_staged_path "$util_name")"
    if [[ ! -f "$staged" ]]; then
      aux_missing+=("$util_name")
    fi
  done
  if ((${#aux_missing[@]} > 0)); then
    show_message "C-engine Bash failed" \
      "Pre-built auxiliary Wasm files missing: ${aux_missing[*]}\n\nRun ./start.sh --build-aux to build them."
    return 1
  fi

  if ! run_logged_step "Build Coreutils corresponding-source package" \
      "$C_ENGINE_BASH_LOG" make -C "$REPO_ROOT/src/html-rt" \
      BUILD_DIR="$HTML_BUILD" coreutils-source-package; then
    show_message "C-engine Bash source package failed" \
      "Could not build the Coreutils corresponding-source archive.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  if ! run_logged_step "Install the generated Bash launch snapshot" \
      "$C_ENGINE_BASH_LOG" python3 "$REPO_ROOT/src/html-rt/tools/vfs.py" \
      install --component launch --source "$C_ENGINE_BASH_RUNTIME_WAST"; then
    show_message "C-engine Bash install failed" \
      "Could not install the launch snapshot into the shared VFS.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  if ! run_logged_step "Install the Bash webapp into the VFS" \
      "$C_ENGINE_BASH_LOG" python3 "$REPO_ROOT/src/html-rt/tools/vfs.py" \
      install --component app --source "$REPO_ROOT/src/html-rt/src"; then
    show_message "C-engine Bash install failed" \
      "Could not install the Bash webapp into /root/waste/app.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  # Copy staging data and amalgamate the HTML page
  if ! run_logged_step "Copy staging data for Bash page" \
      "$C_ENGINE_BASH_LOG" python3 "$C_ENGINE_BASH_GENERATOR" \
      --repo-root "$REPO_ROOT" \
      --wasm "$C_ENGINE_WASM" \
      --launch "$C_ENGINE_BASH_RUNTIME_WAST" \
      --output-dir "$C_ENGINE_STAGING_BASH"; then
    show_message "C-engine Bash staging failed" \
      "Could not copy staging data.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi
  if ! run_logged_step "Amalgamate the self-contained C-engine Bash page" \
      "$C_ENGINE_BASH_LOG" bash "$C_ENGINE_BUILD_SH" bash; then
    show_message "C-engine Bash generation failed" \
      "Could not amalgamate the static page.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  printf 'Completed: %s\n' "$(date --iso-8601=seconds)" >>"$C_ENGINE_BASH_LOG"
  show_message "C-engine Bash generated" \
    "The static page embeds the C engine, Bash, and the installed VFS including libc.so.wasm. It can be opened directly with file:// and requires no server.\n\nOutput: $C_ENGINE_BASH_HTML\nLog: $C_ENGINE_BASH_LOG"
}

check_c_engine_bash() {
  if ! have_command node || ! have_command python3 || [[ ! -f "$C_ENGINE_BASH_BROWSER_TEST" ||
       ! -f "$C_ENGINE_TERMINAL_MODEL_TEST" || ! -f "$C_ENGINE_BASH_HTML" ]]; then
    show_message "C-engine Bash checks" \
      "Node.js, both focused harnesses, and a generated bash.html are required. Build the page with ./start.sh --html-bash."
    return 1
  fi
  : >"$C_ENGINE_BASH_LOG"
  printf 'Focused C-engine Bash checks\nStarted: %s\n\n' \
    "$(date --iso-8601=seconds)" >>"$C_ENGINE_BASH_LOG"

  if ! run_logged_step "Run the C-engine terminal model test" \
      "$C_ENGINE_BASH_LOG" node "$C_ENGINE_TERMINAL_MODEL_TEST"; then
    show_message "C-engine terminal model test failed" \
      "The VT model did not preserve cursor, alternate-screen, character-set, and ncurses REP drawing semantics.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  if ! run_logged_step "Run the C-engine Bash browser smoke test" \
      "$C_ENGINE_BASH_LOG" node "$C_ENGINE_BASH_BROWSER_TEST"; then
    show_message "C-engine Bash browser test failed" \
      "The generated worker did not survive prompt, delayed input, command execution, and exit.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  if ! run_logged_step "Run the C-engine Bash guest redirection test" \
      "$C_ENGINE_BASH_LOG" env \
      WASTE_COREUTILS_LS_COMMAND=': < /usr/share/waste/launch.wast; echo __REDIR_STATUS__:$?' \
      WASTE_COREUTILS_LS_EXPECT='__REDIR_STATUS__:0' \
      WASTE_COREUTILS_LS_EXPECT_COUNT=1 \
      node "$C_ENGINE_BASH_BROWSER_TEST" --coreutils-ls --full-package; then
    show_message "C-engine Bash guest redirection test failed" \
      "Bash could not open the packaged launch file through guest input redirection and report status 0.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  if ! run_logged_step "Run the C-engine Bash process-continuation test" \
      "$C_ENGINE_BASH_LOG" node "$C_ENGINE_BASH_BROWSER_TEST" \
      --missing-command; then
    show_message "C-engine Bash process-continuation test failed" \
      "The generated runtime did not survive assignment, expansion, command-not-found, status propagation, later input, and exit.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  if ! run_logged_step "Run the C-engine Bash readline/select test" \
      "$C_ENGINE_BASH_LOG" node "$C_ENGINE_BASH_BROWSER_TEST" \
      --readline-echo; then
    show_message "C-engine Bash readline/select test failed" \
      "The generated runtime did not display per-key readline input before Enter or resume Bash through the engine select path.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  if ! run_logged_step "Run the C-engine Bash readline completion test" \
      "$C_ENGINE_BASH_LOG" node "$C_ENGINE_BASH_BROWSER_TEST" \
      --readline-completion --full-package; then
    show_message "C-engine Bash readline completion test failed" \
      "The generated runtime did not complete /bin/pw to /bin/pwd through Readline and the engine VFS directory ABI.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  if ! run_logged_step "Run the C-engine Bash readline arrow-key test" \
      "$C_ENGINE_BASH_LOG" node "$C_ENGINE_BASH_BROWSER_TEST" \
      --readline-arrow; then
    show_message "C-engine Bash readline arrow-key test failed" \
      "The generated runtime did not deliver a grouped cursor-key sequence to Readline, recall and execute history, accept a later command, and exit cleanly.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  if ! run_logged_step "Run the C-engine Bash heredoc/pipe test" \
      "$C_ENGINE_BASH_LOG" node "$C_ENGINE_BASH_BROWSER_TEST" \
      --heredoc --full-package; then
    show_message "C-engine Bash heredoc/pipe test failed" \
      "The generated runtime did not pass heredoc input through an engine-owned pipe to packaged Coreutils cat, persist redirected output in the VFS, and exit cleanly.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  if ! run_logged_step "Run the C-engine Bash aggregate command matrix" \
      "$C_ENGINE_BASH_LOG" node "$C_ENGINE_BASH_BROWSER_TEST" \
      --coreutils-matrix --full-package; then
    show_message "C-engine Bash aggregate command matrix failed" \
      "One shell lifetime did not execute all packaged Coreutils utilities plus wat and wast with the expected output and status.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  if ! run_logged_step "Run the C-engine Bash shared-library/Rogue test" \
      "$C_ENGINE_BASH_LOG" node "$C_ENGINE_BASH_BROWSER_TEST" \
      --shared-library; then
    show_message "C-engine Bash shared-library test failed" \
      "The generated runtime did not stage /bin/rogue and libncurses, relocate the PIC executable and shared object, render Rogue, accept terminal input, and return to Bash with status 0.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  local browser_date_year
  browser_date_year="$(date -u +%Y)"
  if ! run_logged_step "Run the C-engine Bash wall-clock/date test" \
      "$C_ENGINE_BASH_LOG" env \
      WASTE_COREUTILS_LS_COMMAND='date -u +%Y' \
      WASTE_COREUTILS_LS_EXPECT="$browser_date_year" \
      node "$C_ENGINE_BASH_BROWSER_TEST" --coreutils-ls --full-package; then
    show_message "C-engine Bash wall-clock/date test failed" \
      "GNU date did not read the current UTC year through the browser Date clock and engine realtime ABI.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  local date_source_epoch date_source_time
  date_source_epoch="$(python3 -c 'import json,sys; print(next(e["mtime_sec"] for e in json.load(open(sys.argv[1]))["entries"] if e["path"] == "/usr/bin/date"))' "$REPO_ROOT/src/vfs/.inventory.json")"
  date_source_time="$(date -u -d "@$date_source_epoch" '+%b %e %H:%M')"
  if ! run_logged_step "Run the C-engine Bash packaged-mtime test" \
      "$C_ENGINE_BASH_LOG" env \
      WASTE_COREUTILS_LS_COMMAND='ls -l /usr/bin/date' \
      WASTE_COREUTILS_LS_EXPECT="$date_source_time" \
      node "$C_ENGINE_BASH_BROWSER_TEST" --coreutils-ls --full-package; then
    show_message "C-engine Bash packaged-mtime test failed" \
      "The static page did not preserve the staged date.wasm source modification time in /usr/bin/date.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  local engine_build_epoch engine_build_time
  engine_build_epoch="$(python3 -c 'import json,sys; print(next(e["mtime_sec"] for e in json.load(open(sys.argv[1]))["entries"] if e["path"] == "/"))' "$REPO_ROOT/src/vfs/.inventory.json")"
  engine_build_time="$(date -u -d "@$engine_build_epoch" '+%b %e %H:%M')"
  if ! run_logged_step "Run the C-engine Bash virtual-node build-mtime test" \
      "$C_ENGINE_BASH_LOG" env \
      WASTE_COREUTILS_LS_COMMAND='ls -ld / /bin /bin/wat /bin/wast' \
      WASTE_COREUTILS_LS_EXPECT="$engine_build_time" \
      WASTE_COREUTILS_LS_EXPECT_COUNT=4 \
      node "$C_ENGINE_BASH_BROWSER_TEST" --coreutils-ls --full-package; then
    show_message "C-engine Bash virtual-node build-mtime test failed" \
      "The root, bin, wat, or wast node did not preserve the installed VFS timestamp.\n\nLog: $C_ENGINE_BASH_LOG"
    return 1
  fi

  printf 'Completed: %s\n' "$(date --iso-8601=seconds)" >>"$C_ENGINE_BASH_LOG"
  show_message "C-engine Bash checks passed" \
    "Focused worker and terminal-model checks passed.\n\nPage: $C_ENGINE_BASH_HTML\nLog: $C_ENGINE_BASH_LOG"
}

check_c_engine_browser_full() {
  if ! have_command node || [[ ! -f "$C_ENGINE_BASH_HTML" ||
       ! -f "$REPO_ROOT/tests/c-engine-offline-browser.cjs" ]]; then
    show_message "Full browser check" \
      "Node.js and a generated bash.html are required. Build the page with ./start.sh --html-bash."
    return 1
  fi
  : >"$C_ENGINE_BASH_LOG"
  printf 'Full offline-browser acceptance\nStarted: %s\n\n' \
    "$(date --iso-8601=seconds)" >>"$C_ENGINE_BASH_LOG"
  run_logged_step "Run full offline-browser acceptance" "$C_ENGINE_BASH_LOG" \
    node "$REPO_ROOT/tests/c-engine-offline-browser.cjs" --suite-full
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
  run_logged_step "Generate core WAST test data" "$TEST_LOG" \
    python3 "$C_ENGINE_GENERATOR" --runner "$C_ENGINE_RUNNER" \
    --wasm "$C_ENGINE_WASM" --tests "$C_ENGINE_CORE_TESTS" \
    --output-dir "$C_ENGINE_STAGING_TESTS" || return 1
  run_logged_step "Exercise core WAST browser workers" "$TEST_LOG" \
    node "$C_ENGINE_BROWSER_TEST"
}

run_test_group() {
  local group="$1"
  if [[ "$group" != c-engine && "$group" != c-engine-core ]] && ! have_command node; then
    show_message "Runtime tests" "Node.js is required to run the runtime test suites."
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
  if [[ "$group" == libc || "$group" == all ]]; then
    run_logged_step "Build guest libc and fixtures" "$TEST_LOG" \
      build_waste_libc true || status=1
  fi
  if ((status != 0)); then
    show_message "Runtime tests failed" \
      "A test prerequisite failed.\n\nLog: $TEST_LOG"
    return 1
  fi

  case "$group" in
    c-engine)
      generate_c_engine_tests || status=1
      ;;
    c-engine-core)
      generate_c_engine_core_tests || status=1
      ;;
    libc)
      run_logged_test "allocator (native)" node "$REPO_ROOT/tests/libc-test/allocator-native.cjs" || status=1
      ;;
    all)
      generate_c_engine_tests || status=1
      run_logged_test "allocator (native)" node "$REPO_ROOT/tests/libc-test/allocator-native.cjs" || status=1
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
      "Tests run locally against the C engine." \
      20 88 8 \
      all "Run C-engine tests and native allocator suite" \
      c-engine "Build C engine and run relaxed-SIMD spec tests" \
      c-engine-core "Run official core WAST browser-worker tests" \
      libc "Run the native allocator suite" \
      log "Show the last test log" \
      back "Return to the main menu" 3>&1 1>&2 2>&3)" || return 0
    case "$choice" in
      all|c-engine|c-engine-core|libc) run_test_group "$choice" || true ;;
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
    choice="$(whiptail --title "WASTE dependencies" --menu \
      "$STATUS_TEXT" 20 82 4 \
      install "Install or repair dependencies" \
      commands "Show installation commands" \
      recheck "Check again" \
      quit "Exit" 3>&1 1>&2 2>&3)" || return 1

    case "$choice" in
      install) install_missing_dependencies || true ;;
      commands)
        show_message "Installation commands" \
          "System:\n$(system_install_command)" ;;
      recheck) ;;
      quit) return 1 ;;
    esac
  done
}

main_menu() {
  while true; do
    local choice
    choice="$(whiptail --title "WASTE" --menu \
      "WebAssembly Threading Environment" 22 78 12 \
      -- \
      sync    "Git pull/rebase with autostash" \
      ---     "── Engine ──────────────────────────────────" \
      cli-compile "Build engine with cli runtime" \
      cli-test    "Run the full test suite in the cli runtime" \
      html-bash   "Build engine and example bash into static HTML" \
      html-check  "Run focused C-engine Bash worker checks" \
      aux         "Build auxiliary Wasm utilities" \
      -----   "────────────────────────────────────────────" \
      quit    "Exit" 3>&1 1>&2 2>&3)" || return 0

    case "$choice" in
      sync) safe_repository_update || true ;;
      cli-compile) compile_cli_engine || true ;;
      cli-test) run_cli_tests || true ;;
      html-bash) generate_c_engine_bash_html || true ;;
      html-check) check_c_engine_bash || true ;;
      aux) aux_menu ;;
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
    --build-libc) build_waste_libc ;;
    --build-aux) build_all_aux ;;
    --c-engine-tests)
      : >"$TEST_LOG"
      c_engine_status=0
      generate_c_engine_tests || c_engine_status=$?
      printf '\nFinished: %s\n' "$(date --iso-8601=seconds)" >>"$TEST_LOG"
      return "$c_engine_status" ;;
    --c-engine-bash-html|--html-bash) generate_c_engine_bash_html ;;
    --html-check) check_c_engine_bash ;;
    --html-browser-full) check_c_engine_browser_full ;;
    --c-engine-core-tests)
      : >"$TEST_LOG"
      c_engine_status=0
      generate_c_engine_core_tests || c_engine_status=$?
      printf '\nFinished: %s\n' "$(date --iso-8601=seconds)" >>"$TEST_LOG"
      return "$c_engine_status" ;;
    --ocaml-reference) make -C "$REPO_ROOT/submodules" ocaml-test ;;
    --html-test|--c-engine-html|--generate-html|--compile|--generate-bash-html|--patch-status|--apply-i31|--revert-i31)
      printf 'error: %s retired; use --cli-test for native tests, --html-bash to build the shell/test page, --html-browser-full for browser acceptance, or --ocaml-reference for the unpatched OCaml reference interpreter\n' "$action" >&2
      return 2 ;;
    wizard)
      dependency_menu || return 1
      main_menu ;;
    *)
      usage >&2
      return 2 ;;
  esac
}

main "$@"
