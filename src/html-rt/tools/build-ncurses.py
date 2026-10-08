#!/usr/bin/env python3
"""Cross-compile ncurses 6.4 for wasm32 as a PIC shared library.

Produces libncurses.so.wasm with a dylink.0 section suitable for loading
via dlopen at runtime.  The built library is staged for VFS inclusion at
/usr/lib/libncurses.so.wasm and curses headers are installed into the
application sysroot.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

from shared_libc import rewrite, check_imports


# Ncurses source files needed for a minimal curses library.  These cover
# the core screen/window API, terminal setup via built-in fallback
# descriptions, and the termcap/terminfo compatibility layer.
#
# Excluded: form, menu, panel, c++, progs, test, Ada95.

NCURSES_BASE_SOURCES = [
    "lib_addch.c", "lib_addstr.c", "lib_beep.c", "lib_bkgd.c",
    "lib_box.c", "lib_chgat.c", "lib_clear.c", "lib_clearok.c",
    "lib_clrbot.c", "lib_clreol.c", "lib_color.c", "lib_colorset.c",
    "lib_delch.c", "lib_delwin.c", "lib_dft_fgbg.c", "lib_echo.c",
    "lib_endwin.c", "lib_erase.c", "lib_flash.c", "lib_freeall.c",
    "lib_getch.c", "lib_getstr.c", "lib_hline.c", "lib_immedok.c",
    "lib_inchstr.c", "lib_initscr.c", "lib_insch.c", "lib_insdel.c",
    "lib_insnstr.c", "lib_instr.c", "lib_isendwin.c", "lib_leaveok.c",
    "key_defined.c", "keybound.c", "keyok.c",
    "lib_mouse.c", "lib_move.c", "lib_mvwin.c", "lib_newterm.c",
    "lib_newwin.c", "lib_nl.c", "lib_overlay.c", "lib_pad.c",
    "lib_printw.c", "lib_redrawln.c", "lib_refresh.c", "lib_restart.c",
    "lib_scanw.c", "lib_screen.c", "lib_scroll.c", "lib_scrollok.c",
    "lib_scrreg.c", "lib_set_term.c", "lib_slk.c", "lib_slkatr_set.c",
    "lib_slkatrof.c", "lib_slkatron.c", "lib_slkatrset.c",
    "lib_slkattr.c", "lib_slkclear.c", "lib_slkcolor.c",
    "lib_slkinit.c", "lib_slklab.c", "lib_slkrefr.c", "lib_slkset.c",
    "lib_slktouch.c", "lib_touch.c", "lib_ungetch.c", "version.c",
    "lib_vline.c", "lib_wattroff.c", "lib_wattron.c", "lib_winch.c",
    "lib_window.c", "nc_panel.c", "safe_sprintf.c", "tries.c",
    "wresize.c",
]

NCURSES_TTY_SOURCES = [
    "hardscroll.c", "hashmap.c", "lib_mvcur.c", "lib_tstp.c", "lib_twait.c",
    "lib_vidattr.c", "tty_update.c",
]

NCURSES_TINFO_SOURCES = [
    "access.c", "add_tries.c", "alloc_entry.c", "alloc_ttype.c",
    "captoinfo.c", "comp_captab.c", "comp_error.c", "comp_expand.c",
    "comp_hash.c", "comp_parse.c", "comp_scan.c", "comp_userdefs.c",
    "db_iterator.c", "doalloc.c", "entries.c", "free_ttype.c",
    "getenv_num.c", "hashed_db.c", "home_terminfo.c",
    "init_keytry.c", "lib_acs.c", "lib_baudrate.c", "lib_cur_term.c",
    "lib_data.c", "lib_has_cap.c", "lib_kernel.c", "lib_longname.c",
    "lib_napms.c", "lib_options.c", "lib_print.c", "lib_raw.c",
    "lib_setup.c", "lib_termcap.c", "lib_termname.c", "lib_tgoto.c",
    "lib_ti.c", "lib_tparm.c", "lib_tputs.c", "lib_ttyflags.c",
    "name_match.c", "obsolete.c", "parse_entry.c",
    "read_entry.c", "read_termcap.c", "strings.c", "trim_sgr0.c",
    "use_screen.c", "write_entry.c",
]

NCURSES_TRACE_SOURCES = [
    # tty_update uses _nc_visbuf and several non-trace sources reference the
    # always-defined _nc_tracing variable even in a non-tracing build.
    "lib_trace.c", "visbuf.c",
]

# Generated source files that ncurses configure/make produce.
NCURSES_GENERATED = [
    "codes.c", "comp_captab.c", "comp_userdefs.c", "fallback.c",
    "init_keytry.h", "keys.list", "lib_gen.c", "lib_keyname.c",
    "names.c", "unctrl.c",
]


CONFIG_SITE_CONTENT = """\
# Cross-compilation answers for ncurses on wasm32-waste.
ac_cv_prog_cc_cross=yes
ac_cv_c_cross=yes
ac_cv_c_bigendian=no
# The guest libc provides termios via waste_kernel host functions.
cf_cv_have_tcgetattr=yes
# WebAssembly has no signals.
ac_cv_func_sigaction=no
ac_cv_func_sigvec=no
ac_cv_func_sigprocmask=no
# No process control.
ac_cv_func_vfork=no
ac_cv_func_fork=no
ac_cv_func_gettimeofday=yes
ac_cv_func_poll=no
ac_cv_func_select=yes
# No locale beyond C.UTF-8.
ac_cv_func_setlocale=yes
# No mmap in guest libc.
ac_cv_func_mmap=no
# No symlinks or links in the VFS.
ac_cv_func_link=no
ac_cv_func_symlink=no
ac_cv_func_readlink=yes
# No shared library dlsym.
ac_cv_func_dlsym=no
# No regex in guest libc for now — ncurses doesn't strictly need it.
ac_cv_func_regcomp=no
# The nanosleep stub is available.
ac_cv_func_nanosleep=yes
# No BSD tty interfaces.
ac_cv_header_sys_ioctl_h=yes
# Sizes.
ac_cv_sizeof_signed_char=1
ac_cv_type_sigaction=no
cf_cv_typeof_chtype=int
cf_cv_typeof_mmask_t=int
cf_cv_1UL="1UL"
cf_cv_type_of_bool=unsigned
cf_cv_header_stdbool_h=1
cf_cv_builtin_bool=1
# Working putenv and setenv.
ac_cv_func_putenv=yes
ac_cv_func_setenv=yes
# WASTE does not yet provide the search(3) tree API.  Ncurses has an
# allocation-free fallback when this configure result is false.
ac_cv_func_tsearch=no
"""


def ensure_sysroot(repo_root: Path, output_base: Path) -> Path:
    """Refresh from the mounted SDK; never reuse an unchecked old sysroot."""
    build_sysroot = repo_root / "src" / "html-rt" / "tools" / "build-waste-sysroot.py"
    sysroot = output_base / "sysroot"
    subprocess.run(
        [sys.executable, str(build_sysroot),
         "--repo-root", str(repo_root),
         "--output", str(sysroot)],
        check=True,
    )
    return sysroot


def configure_ncurses(
    ncurses_source: Path,
    build_dir: Path,
    sysroot: Path,
    config_site: Path,
) -> None:
    """Run ncurses configure for wasm32 cross-compilation."""
    cc = str(sysroot / "bin" / "waste-wasm-clang")
    env = os.environ.copy()
    env.update({
        "CONFIG_SITE": str(config_site),
        "CC": cc,
        "CPP": cc + " -E",
        "AR": "llvm-ar",
        "RANLIB": "llvm-ranlib",
        "LD": "wasm-ld",
        # ncurses configure checks for these; suppress them in cross mode.
        "CFLAGS": "-O2 -DNDEBUG",
        "LDFLAGS": "",
    })
    configure = ncurses_source / "configure"
    command = [
        str(configure),
        "--host=wasm32-unknown-none",
        "--prefix=/usr",
        "--without-shared",
        "--without-cxx",
        "--without-cxx-binding",
        "--without-ada",
        "--without-manpages",
        "--without-progs",
        "--without-tack",
        "--without-tests",
        "--without-dlsym",
        "--disable-database",
        "--enable-termcap",
        "--with-fallbacks=xterm,xterm-256color,vt100,dumb",
        "--with-default-terminfo-dir=/usr/share/terminfo",
        "--disable-db-install",
        "--disable-home-terminfo",
        "--disable-stripping",
        "--with-bool=unsigned",
    ]
    log = build_dir / "configure.log"
    with log.open("w", encoding="utf-8") as logf:
        result = subprocess.run(
            command, cwd=build_dir, env=env,
            stdout=logf, stderr=subprocess.STDOUT, check=False,
        )
    if result.returncode != 0:
        print(f"configure failed (exit {result.returncode}), log: {log}")
        # Print last 40 lines of the log for diagnosis.
        lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
        for line in lines[-40:]:
            print(f"  {line}")
        raise SystemExit(1)
    print("configure: ok")


def generate_fallbacks(
    ncurses_source: Path,
    build_dir: Path,
    sysroot: Path,
) -> None:
    """Generate the fallback.c file with built-in terminal descriptions.

    ncurses's make normally runs tic to compile terminal descriptions.
    For cross-compilation we generate a minimal fallback from the
    upstream template with hardcoded entries.
    """
    fallback_c = build_dir / "ncurses" / "fallback.c"
    if (fallback_c.is_file()
            and "static const TERMTYPE2 fallbacks[" in
            fallback_c.read_text(encoding="utf-8", errors="replace")):
        return
    fallback_c.parent.mkdir(parents=True, exist_ok=True)
    # Use the ncurses-provided MKfallback.sh if tic is available,
    # otherwise generate a stub that results in an empty fallback table.
    tic_path = shutil.which("tic")
    infocmp_path = shutil.which("infocmp")
    if tic_path and infocmp_path:
        env = os.environ.copy()
        env["TERMINFO"] = "/usr/share/terminfo"
        try:
            result = subprocess.run(
                ["sh", str(ncurses_source / "ncurses" / "tinfo" / "MKfallback.sh"),
                 "/usr/share/terminfo",
                 str(ncurses_source / "misc" / "terminfo.src"),
                 str(tic_path), str(infocmp_path),
                 "xterm", "xterm-256color", "vt100", "dumb"],
                capture_output=True, text=True, env=env, check=False,
            )
            if (result.returncode == 0
                    and "static const TERMTYPE2 fallbacks[4]" in result.stdout
                    and "count total Booleans" in result.stdout):
                fallback_c.write_text(result.stdout, encoding="utf-8")
                print(f"fallback.c: generated with infocmp ({len(result.stdout)} bytes)")
                return
        except Exception:
            pass
    # Stub fallback — just an empty table.
    # Note: curses.priv.h may define _nc_fallback2 as a macro expanding
    # to _nc_fallback, so we only provide _nc_fallback to avoid
    # redefinition errors.
    fallback_c.write_text(
        '#include "curses.priv.h"\n\n'
        "NCURSES_EXPORT(const TERMTYPE *)\n"
        "_nc_fallback(const char *name)\n"
        "{\n"
        "    (void)name;\n"
        "    return (const TERMTYPE *)0;\n"
        "}\n",
        encoding="utf-8",
    )
    print("fallback.c: generated empty stub")


def compile_ncurses(
    ncurses_source: Path,
    build_dir: Path,
    sysroot: Path,
) -> list[Path]:
    """Compile ncurses source files to wasm32 object files."""
    cc = str(sysroot / "bin" / "waste-wasm-clang")
    obj_dir = build_dir / "objects"
    obj_dir.mkdir(parents=True, exist_ok=True)

    # Build include paths: the configure-generated headers come from
    # build_dir, the ncurses private headers from the source tree.
    include_flags = [
        f"-I{build_dir / 'include'}",
        f"-I{build_dir / 'ncurses'}",
        f"-I{ncurses_source / 'include'}",
        f"-I{ncurses_source / 'ncurses'}",
        f"-I{ncurses_source / 'ncurses' / 'tinfo'}",
    ]

    # PIC flags for shared library: import memory and table bases.
    pic_flags = ["-fPIC", "-fvisibility=default"]

    # Ncurses needs these defines.
    defines = [
        "-include", "stdbool.h",  # consume C11 bool before raw curses chooses its DSO ABI
        "-DHAVE_CONFIG_H",
        "-DNDEBUG",
        "-DBUILDING_NCURSES",
    ]

    objects = []
    source_dirs = {
        "base": (ncurses_source / "ncurses" / "base", NCURSES_BASE_SOURCES),
        "tty": (ncurses_source / "ncurses" / "tty", NCURSES_TTY_SOURCES),
        "tinfo": (ncurses_source / "ncurses" / "tinfo", NCURSES_TINFO_SOURCES),
        "trace": (ncurses_source / "ncurses" / "trace", NCURSES_TRACE_SOURCES),
    }

    # Compile generated sources (from the build dir).
    generated_c = [
        "ncurses/lib_gen.c", "ncurses/lib_keyname.c",
        "ncurses/names.c", "ncurses/codes.c", "ncurses/unctrl.c",
        "ncurses/fallback.c",
    ]

    all_sources: list[tuple[Path, str]] = []
    for group_name, (src_dir, filenames) in source_dirs.items():
        for name in filenames:
            source = src_dir / name
            if source.is_file():
                all_sources.append((source, f"{group_name}_{name}"))
            else:
                # Some files may have been generated by configure in the
                # build tree instead.
                alt = build_dir / "ncurses" / name
                if alt.is_file():
                    all_sources.append((alt, f"{group_name}_{name}"))
                else:
                    print(f"  warning: skipping {name} (not found)")

    for gen_path in generated_c:
        source = build_dir / gen_path
        if source.is_file():
            all_sources.append((source, source.name))

    # A PIC library cannot use the main executable's CRT-owned stdio slots
    # while it is being instantiated.  Give ncurses its own slots and bind
    # them to the process libc from its constructor.
    shared_stdio = build_dir / "ncurses" / "waste_shared_stdio.c"
    shared_stdio.write_text(
        "#include <stdio.h>\n"
        "extern FILE *waste_stdin(void);\n"
        "extern FILE *waste_stdout(void);\n"
        "extern FILE *waste_stderr(void);\n"
        "FILE *stdin; FILE *stdout; FILE *stderr;\n"
        "__attribute__((constructor)) static void waste_shared_stdio_init(void) {\n"
        "  stdin = waste_stdin(); stdout = waste_stdout(); stderr = waste_stderr();\n"
        "}\n",
        encoding="utf-8",
    )
    all_sources.append((shared_stdio, "waste_shared_stdio.c"))

    failed = []
    for source, label in all_sources:
        obj = obj_dir / (label.replace(".c", ".o"))
        cmd = [cc, "-c", "-O2"] + defines + include_flags + pic_flags + [
            "-Wno-unused-parameter",
            "-Wno-sign-compare",
            "-Wno-implicit-function-declaration",
            "-Wno-int-conversion",
            str(source), "-o", str(obj),
        ]
        result = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            failed.append((label, result.stderr))
        else:
            objects.append(obj)

    if failed:
        print(f"\ncompile: {len(objects)} ok, {len(failed)} failed")
        for name, err in failed:
            print(f"  FAIL {name}: {err.strip()}")
        raise RuntimeError("ncurses compilation failed; refusing partial library publication")
    else:
        print(f"compile: {len(objects)} objects ok")
    return objects


def link_shared_library(
    objects: list[Path],
    output: Path,
) -> None:
    """Link object files into a PIC shared library with dylink.0."""
    command = [
        "wasm-ld",
        "--shared",
        "--import-memory",
        "--import-table",
        "--export-all",
        "--allow-undefined",
        "--no-entry",
        "-o", str(output),
    ] + [str(o) for o in objects]
    subprocess.run(command, check=True)
    rewrite(output, Path(__file__).resolve().parents[3] / "src/vfs/lib/libc.so.wasm")
    size = output.stat().st_size
    print(f"link: {output.name} ({size} bytes)")


def install_headers(build_dir: Path, ncurses_source: Path, sysroot: Path) -> None:
    """Public snapshots are published only by build-guest-sdk.py --install.

    Generated ncurses_cfg.h and implementation headers remain in build_dir.
    Do not overwrite the canonical SDK wrapper or package build sysroots.
    """
    print("headers: generated in build tree; explicit guest-sdk-install publishes selected public headers")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", action="store_true", help="Explicitly install audited outputs into src/vfs")
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path,
                        default=Path("build/ncurses"))
    args = parser.parse_args()
    repo_root = args.repo_root.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    ncurses_source = repo_root / "submodules" / "ncurses"
    if not (ncurses_source / "configure").is_file():
        print(f"error: ncurses source not found at {ncurses_source}")
        return 1

    # Ensure sysroot.
    sysroot = ensure_sysroot(repo_root, output)
    print(f"sysroot: {sysroot}")

    # Write config.site.
    config_site = output / "config.site"
    config_site.write_text(CONFIG_SITE_CONTENT, encoding="utf-8")

    # Configure.
    build_dir = output / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    if not (build_dir / "include" / "curses.h").is_file():
        configure_ncurses(ncurses_source, build_dir, sysroot, config_site)
    else:
        print("configure: using cached results")

    # Old cached configure trees may predate the tsearch=no policy above.
    cfg_h = build_dir / "include" / "ncurses_cfg.h"
    if cfg_h.is_file():
        cfg = cfg_h.read_text(encoding="utf-8")
        cfg = cfg.replace("#define HAVE_TSEARCH 1", "#define HAVE_TSEARCH 0")
        cfg_h.write_text(cfg, encoding="utf-8")

    # Run make to generate the source files that configure set up.
    # We only need the generated headers and a few source files.
    make_env = os.environ.copy()
    cc = str(sysroot / "bin" / "waste-wasm-clang")
    make_env["CC"] = cc
    # Run make in the include directory to generate headers.
    subprocess.run(
        ["make", "-C", str(build_dir / "include"), "-j4"],
        env=make_env, capture_output=True, check=False,
    )
    # Run make in the ncurses directory to generate source files.
    # Use -k to continue on errors (some targets may fail in cross-build).
    # Note: fallback.c is NOT included here — we generate it ourselves
    # because MKfallback.sh produces invalid output when infocmp cannot
    # dump full terminal descriptions.
    subprocess.run(
        ["make", "-C", str(build_dir / "ncurses"),
         "lib_gen.c", "lib_keyname.c",
         "names.c", "codes.c", "unctrl.c", "init_keytry.h", "keys.list",
         "comp_captab.c", "comp_userdefs.c"],
        env=make_env, capture_output=True, check=False,
    )

    # Generate fallback terminal descriptions AFTER make, so make cannot
    # overwrite our stub with its broken MKfallback.sh output.
    generate_fallbacks(ncurses_source, build_dir, sysroot)

    # Compile.
    objects = compile_ncurses(ncurses_source, build_dir, sysroot)
    if not objects:
        print("error: no objects compiled")
        return 1

    # Link.
    lib_output = output / "libncurses.so.wasm"
    link_shared_library(objects, lib_output)

    # Install headers into sysroot.
    install_headers(build_dir, ncurses_source, sysroot)

    # Stage for VFS inclusion.
    vfs_stage = output / "vfs"
    vfs_stage.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(lib_output, vfs_stage / "libncurses.so.wasm")
    print(f"\nstaged: {vfs_stage / 'libncurses.so.wasm'}")
    print("VFS path: /usr/lib/libncurses.so.wasm")
    if args.install:
        check_imports([lib_output], repo_root / "src/vfs/lib/libc.so.wasm")
        subprocess.run([sys.executable, str(repo_root / "src/html-rt/tools/build-guest-sdk.py"),
                        "--install", "--library",
                        str(vfs_stage / "libncurses.so.wasm")], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
