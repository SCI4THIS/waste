#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
COREUTILS_SUBMODULE="$SCRIPT_DIR/coreutils"
COREUTILS_PATCH="$SCRIPT_DIR/coreutils-waste.patch"
COREUTILS_BUILD="${WASTE_COREUTILS_BUILD_DIR:-$REPO_ROOT/build/html-rt/coreutils}"
STAGED_SOURCE="$COREUTILS_BUILD/source"
SOURCE_STAMP="$STAGED_SOURCE/.waste-bootstrap-source"

REQUIRED_COMMANDS=(
  autoconf automake autopoint bison gettext git gperf gzip libtoolize m4
  makeinfo patch perl tar texi2pdf wget xz
)

MODE=bootstrap
INSTALL_MISSING=true
FORCE=false

usage() {
  cat <<'EOF'
Usage: submodules/bootstrap-coreutils.sh [OPTION]

Install the coreutils developer prerequisites when needed, stage the pinned
source under build/, and generate configure there using the checked-in Gnulib.
The coreutils submodule is never modified.

  --check        report missing tools and whether staged configure is current
  --no-install   do not install missing system packages
  --install-only install missing packages without staging or bootstrapping
  --force        restage and regenerate configure
  --help         show this help
EOF
}

have_command() {
  command -v "$1" >/dev/null 2>&1
}

missing_commands() {
  local command_name
  for command_name in "${REQUIRED_COMMANDS[@]}"; do
    have_command "$command_name" || printf '%s\n' "$command_name"
  done
}

package_manager() {
  if have_command pacman; then printf '%s\n' pacman
  elif have_command apt-get; then printf '%s\n' apt
  elif have_command dnf; then printf '%s\n' dnf
  elif have_command zypper; then printf '%s\n' zypper
  else printf '%s\n' unknown
  fi
}

run_privileged() {
  if [[ ${EUID:-$(id -u)} -eq 0 ]]; then
    "$@"
  elif have_command sudo; then
    sudo "$@"
  else
    printf 'error: installing dependencies requires root or sudo\n' >&2
    return 1
  fi
}

install_dependencies() {
  local manager
  manager="$(package_manager)"
  case "$manager" in
    pacman)
      run_privileged pacman -S --needed autoconf automake gettext libtool \
        gperf wget bison gzip m4 texinfo patch perl tar xz
      ;;
    apt)
      run_privileged apt-get install -y autoconf automake autopoint gettext \
        libtool gperf wget bison gzip m4 texinfo patch perl tar xz-utils
      ;;
    dnf)
      run_privileged dnf install -y autoconf automake gettext-devel libtool \
        gperf wget bison gzip m4 texinfo texinfo-tex patch perl tar xz
      ;;
    zypper)
      run_privileged zypper --non-interactive install autoconf automake \
        gettext-tools libtool gperf wget bison gzip m4 makeinfo texlive \
        patch perl tar xz
      ;;
    *)
      printf 'error: unsupported package manager; missing commands:\n' >&2
      missing_commands | sed 's/^/  /' >&2
      return 1
      ;;
  esac
}

source_identity() {
  local commit patch_hash
  commit="$(git -C "$COREUTILS_SUBMODULE" rev-parse HEAD)"
  patch_hash="$(sha256sum "$COREUTILS_PATCH" | awk '{print $1}')"
  printf '%s %s\n' "$commit" "$patch_hash"
}

stage_is_current() {
  [[ -f "$STAGED_SOURCE/configure.ac" && -f "$SOURCE_STAMP" ]] || return 1
  [[ "$(<"$SOURCE_STAMP")" == "$(source_identity)" ]]
}

stage_source() {
  make -C "$REPO_ROOT/src/html-rt" \
    COREUTILS_BUILD_DIR="$COREUTILS_BUILD" coreutils-stage
  printf '%s\n' "$(source_identity)" >"$SOURCE_STAMP"
}

bootstrap_source() {
  (
    cd "$STAGED_SOURCE"
    ./bootstrap --gen --copy --no-bootstrap-sync --no-git --skip-po \
      --gnulib-srcdir="$STAGED_SOURCE/gnulib"
  )
  test -f "$STAGED_SOURCE/configure"
}

while (($#)); do
  case "$1" in
    --check) MODE=check ;;
    --no-install) INSTALL_MISSING=false ;;
    --install-only) MODE=install ;;
    --force) FORCE=true ;;
    --help) usage; exit 0 ;;
    *) printf 'error: unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

mapfile -t MISSING < <(missing_commands)
if [[ "$MODE" == check ]]; then
  if ((${#MISSING[@]})); then
    printf 'missing tools: %s\n' "${MISSING[*]}"
  else
    printf 'bootstrap tools: ready\n'
  fi
  if stage_is_current && [[ -f "$STAGED_SOURCE/configure" ]]; then
    printf 'staged configure: current\n'
  else
    printf 'staged configure: missing or stale\n'
  fi
  ((${#MISSING[@]} == 0)) && stage_is_current && \
    [[ -f "$STAGED_SOURCE/configure" ]]
  exit
fi

if ((${#MISSING[@]})); then
  if [[ "$INSTALL_MISSING" != true ]]; then
    printf 'error: missing tools: %s\n' "${MISSING[*]}" >&2
    exit 1
  fi
  printf 'Installing missing coreutils bootstrap dependencies: %s\n' \
    "${MISSING[*]}"
  install_dependencies
  mapfile -t MISSING < <(missing_commands)
  if ((${#MISSING[@]})); then
    printf 'error: tools remain missing after package installation: %s\n' \
      "${MISSING[*]}" >&2
    exit 1
  fi
fi

if [[ "$MODE" == install ]]; then
  printf 'Coreutils bootstrap dependencies are installed.\n'
  exit 0
fi

if [[ "$FORCE" == true ]] || ! stage_is_current; then
  printf 'Staging the pinned coreutils source...\n'
  stage_source
fi

if [[ "$FORCE" == true || ! -f "$STAGED_SOURCE/configure" ]]; then
  printf 'Generating staged coreutils configure...\n'
  bootstrap_source
else
  printf 'Staged coreutils configure is already current.\n'
fi

test -z "$(git -C "$COREUTILS_SUBMODULE" status --porcelain=v1)"
printf 'configure: %s\n' "$STAGED_SOURCE/configure"
