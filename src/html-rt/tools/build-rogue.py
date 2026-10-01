#!/usr/bin/env python3
"""Cross-compile rogue 5.4.4 for wasm32 as a WASTE executable.

Produces rogue.wasm linked against the ncurses shared library.  Ncurses
symbols are imported under the module name "libncurses" so the engine
loader can resolve them via dlopen at runtime.
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path


# All rogue source files.
ROGUE_SOURCES = [
    "armor.c", "chase.c", "command.c", "daemon.c", "daemons.c",
    "extern.c", "fight.c", "init.c", "io.c", "list.c",
    "mach_dep.c", "main.c", "mdport.c", "misc.c", "monsters.c",
    "move.c", "new_level.c", "options.c", "pack.c", "passages.c",
    "potions.c", "rings.c", "rip.c", "rooms.c", "save.c",
    "scrolls.c", "state.c", "sticks.c", "things.c", "vers.c",
    "weapons.c", "wizard.c", "xcrypt.c",
]


# Minimal config.h for wasm32 cross-compilation.  Disables features that
# require real POSIX process control, filesystem access, and signals.
CONFIG_H = """\
/* config.h — Generated for wasm32-waste cross-compilation. */
#ifndef ROGUE_CONFIG_H
#define ROGUE_CONFIG_H

#define HAVE_SYS_TYPES_H 1
#define HAVE_STRING_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_FCNTL_H 1
#define HAVE_MEMSET 1
#define HAVE_STRCHR 1
#define HAVE_STRERROR 1
#define HAVE_VPRINTF 1
#define HAVE_UNISTD_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_TERMIOS_H 1
#define HAVE_TERM_H 1
#define HAVE_SETENV 1

/* ncurses functions available via the shared library. */
#define HAVE_ERASECHAR 1
#define HAVE_KILLCHAR 1
#define HAVE_ESCDELAY 1
#define HAVE_NCURSES_H 1

/* Not available in the WASTE guest environment. */
/* #undef HAVE_WORKING_FORK */
/* #undef HAVE_PWD_H */
/* #undef HAVE_GETPWUID */
/* #undef HAVE_GETUID */
/* #undef HAVE_GETGID */
/* #undef HAVE_SETUID */
/* #undef HAVE_SETGID */
/* #undef HAVE_SETREUID */
/* #undef HAVE_SETREGID */
/* #undef HAVE_GETPASS */
/* #undef HAVE_ALARM */
/* #undef HAVE_GETLOADAVG */
/* #undef HAVE_ARPA_INET_H */
/* #undef HAVE_SYS_UTSNAME_H */
/* #undef HAVE_PROCESS_H */
/* #undef HAVE_SYS_IOCTL_H */

/* No scoreboard, lockfile, wizard mode, load/user limits. */
/* #undef SCOREFILE */
/* #undef LOCKFILE */
/* #undef MASTER */
/* #undef MAXLOAD */
/* #undef MAXUSERS */
/* #undef CHECKTIME */
/* #undef DUMP */

#define NUMSCORES 10
#define NUMNAME "Ten"
#define ALLSCORES 1

/* PATH_MAX is not in the guest libc limits.h. */
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define RETSIGTYPE void

#define PACKAGE_NAME "Rogue"
#define PACKAGE_VERSION "5.4.4"
#define PACKAGE_STRING "Rogue 5.4.4"
#define PACKAGE_TARNAME "rogue"

#endif /* ROGUE_CONFIG_H */
"""


# ncurses's curses.h defines `bool` as `NCURSES_BOOL` (typedef unsigned, 4
# bytes) because the ncurses shared library was built with
# `--with-bool=unsigned`. The waste sysroot's <string.h> transitively
# includes clang's <stdbool.h>, which does `#define bool _Bool` (1 byte).
# Rogue's source files include <string.h> and <curses.h> in inconsistent
# orders — files where <string.h> comes after <curses.h> end up with
# bool=_Bool, shifting the THING._t_stats offset by 4 bytes and corrupting
# all creature struct accesses. Force-include this header so clang's
# stdbool.h is short-circuited and bool is consistently NCURSES_BOOL.
ROGUE_BOOL_FIX_H = """\
#ifndef ROGUE_BOOL_FIX_H
#define ROGUE_BOOL_FIX_H
#include <curses.h>
#ifndef __STDBOOL_H
#define __STDBOOL_H
#define __bool_true_false_are_defined 1
#define true 1
#define false 0
#endif
#endif
"""


# Source patch for main.c: replace direct curscr struct access with
# wmove() which works with ncurses's opaque WINDOW structs.
MAIN_C_PATCHES = [
    # The tstp() function accesses curscr->_cury and curscr->_curx directly.
    # Replace with wmove(curscr, oy, ox) which is the portable equivalent.
    (
        "    curscr->_cury = oy;\n    curscr->_curx = ox;\n",
        "    wmove(curscr, oy, ox);\n",
    ),
]

# Patches for mdport.c: replace getpwuid() calls (which require pwd.h and
# a working passwd database) with environment variable fallbacks.
COMMAND_C_PATCHES = [
    # 1. Add #include <stdio.h> and the dump_game_state() function before the
    #    command() function.
    (
        '#include <ctype.h>\n#include "rogue.h"\n\n/*\n * command:',
        '#include <ctype.h>\n#include <stdio.h>\n#include "rogue.h"\n'
        "\n"
        "/*\n"
        " * dump_game_state:\n"
        " *\tWrite a comprehensive game state snapshot to /tmp/rogue.dump.\n"
        " *\tBound to 'g' for debugging the wasm build.\n"
        " */\n"
        "static void\n"
        "dump_game_state(void)\n"
        "{\n"
        "    FILE *fp;\n"
        "    int y, x, i;\n"
        "    THING *tp;\n"
        "    int monster_count, object_count;\n"
        '    static const char *hunger_names[] = {"normal", "hungry", "weak", "faint"};\n'
        "\n"
        '    fp = fopen("/tmp/rogue.dump", "w");\n'
        "    if (!fp) {\n"
        '\tmsg("dump: cannot open /tmp/rogue.dump");\n'
        "\treturn;\n"
        "    }\n"
        "\n"
        '    fprintf(fp, "=== ROGUE GAME STATE DUMP ===\\n\\n");\n'
        "\n"
        "    /* ---- Player ---- */\n"
        '    fprintf(fp, "--- Player ---\\n");\n'
        '    fprintf(fp, "Position: (%d, %d)\\n", hero.y, hero.x);\n'
        '    fprintf(fp, "Dungeon level: %d  (max visited: %d)\\n", level, max_level);\n'
        '    fprintf(fp, "HP: %d/%d\\n", pstats.s_hpt, pstats.s_maxhp);\n'
        '    fprintf(fp, "Strength: %u (max: %u)\\n",\n'
        "\t    pstats.s_str, max_stats.s_str);\n"
        '    fprintf(fp, "Armor class: %d\\n", cur_armor ? cur_armor->o_arm\n'
        "\t    + a_class[cur_armor->o_which] : pstats.s_arm);\n"
        '    fprintf(fp, "Experience: %d (level %d)\\n",\n'
        "\t    pstats.s_exp, pstats.s_lvl);\n"
        '    fprintf(fp, "Gold: %d\\n", purse);\n'
        '    fprintf(fp, "Items in pack: %d\\n", inpack);\n'
        '    fprintf(fp, "Hunger: %s (food_left=%d)\\n",\n'
        "\t    hungry_state >= 0 && hungry_state <= 3\n"
        '\t    ? hunger_names[hungry_state] : "?", food_left);\n'
        '    fprintf(fp, "Has amulet: %s\\n", amulet ? "yes" : "no");\n'
        "\n"
        "    /* Player flags */\n"
        '    fprintf(fp, "Flags:");\n'
        '    if (on(player, ISBLIND))  fprintf(fp, " BLIND");\n'
        '    if (on(player, ISHASTE))  fprintf(fp, " HASTE");\n'
        '    if (on(player, ISHELD))   fprintf(fp, " HELD");\n'
        '    if (on(player, ISHUH))    fprintf(fp, " CONFUSED");\n'
        '    if (on(player, ISLEVIT))  fprintf(fp, " LEVIT");\n'
        '    if (on(player, ISINVIS))  fprintf(fp, " INVIS");\n'
        '    if (on(player, ISREGEN))  fprintf(fp, " REGEN");\n'
        '    if (on(player, SEEMONST)) fprintf(fp, " SEEMONST");\n'
        '    if (on(player, ISFLY))    fprintf(fp, " FLY");\n'
        '    if (player.t_flags == 0)  fprintf(fp, " (none)");\n'
        '    fprintf(fp, " [0x%x]\\n", player.t_flags);\n'
        "\n"
        "    /* Equipment */\n"
        '    fprintf(fp, "\\n--- Equipment ---\\n");\n'
        "    if (cur_weapon)\n"
        '\tfprintf(fp, "Weapon: %s\\n", inv_name(cur_weapon, FALSE));\n'
        "    else\n"
        '\tfprintf(fp, "Weapon: (none)\\n");\n'
        "    if (cur_armor)\n"
        '\tfprintf(fp, "Armor: %s\\n", inv_name(cur_armor, FALSE));\n'
        "    else\n"
        '\tfprintf(fp, "Armor: (none)\\n");\n'
        "    if (cur_ring[LEFT])\n"
        '\tfprintf(fp, "Left ring: %s\\n", inv_name(cur_ring[LEFT], FALSE));\n'
        "    else\n"
        '\tfprintf(fp, "Left ring: (none)\\n");\n'
        "    if (cur_ring[RIGHT])\n"
        '\tfprintf(fp, "Right ring: %s\\n", inv_name(cur_ring[RIGHT], FALSE));\n'
        "    else\n"
        '\tfprintf(fp, "Right ring: (none)\\n");\n'
        "\n"
        "    /* Inventory */\n"
        '    fprintf(fp, "\\n--- Inventory ---\\n");\n'
        "    for (tp = player.t_pack; tp != NULL; tp = next(tp))\n"
        '\tfprintf(fp, "  %c) %s\\n", tp->o_packch, inv_name(tp, FALSE));\n'
        "\n"
        "    /* ---- Rooms ---- */\n"
        '    fprintf(fp, "\\n--- Rooms ---\\n");\n'
        "    for (i = 0; i < MAXROOMS; i++) {\n"
        "\tstruct room *rp = &rooms[i];\n"
        '\tfprintf(fp, "Room %d: pos=(%d,%d) size=(%d,%d) flags=0x%x",\n'
        "\t\ti, rp->r_pos.y, rp->r_pos.x,\n"
        "\t\trp->r_max.y, rp->r_max.x, rp->r_flags);\n"
        '\tif (rp->r_flags & ISDARK) fprintf(fp, " DARK");\n'
        '\tif (rp->r_flags & ISGONE) fprintf(fp, " GONE");\n'
        '\tif (rp->r_flags & ISMAZE) fprintf(fp, " MAZE");\n'
        "\tif (rp->r_goldval > 0)\n"
        '\t    fprintf(fp, " gold=%d@(%d,%d)", rp->r_goldval,\n'
        "\t\t    rp->r_gold.y, rp->r_gold.x);\n"
        '\tfprintf(fp, " exits=%d", rp->r_nexits);\n'
        "\tfor (int e = 0; e < rp->r_nexits; e++)\n"
        '\t    fprintf(fp, " (%d,%d)", rp->r_exit[e].y, rp->r_exit[e].x);\n'
        '\tfprintf(fp, "\\n");\n'
        "    }\n"
        "    if (player.t_room)\n"
        '\tfprintf(fp, "Player in room %d\\n",\n'
        "\t\t(int)(player.t_room - rooms));\n"
        '    fprintf(fp, "Stairs at: (%d, %d)\\n", stairs.y, stairs.x);\n'
        "\n"
        "    /* ---- Monsters ---- */\n"
        '    fprintf(fp, "\\n--- Monsters ---\\n");\n'
        "    monster_count = 0;\n"
        "    for (tp = mlist; tp != NULL; tp = next(tp)) {\n"
        "\tmonster_count++;\n"
        '\tfprintf(fp, "  %c %-16s pos=(%d,%d) hp=%d/%d lvl=%d flags=0x%x",\n'
        "\t\ttp->t_type ? tp->t_type : \'?\',\n"
        "\t\t(tp->t_type >= 'A' && tp->t_type <= 'Z')\n"
        "\t\t    ? monsters[tp->t_type - 'A'].m_name : \"???\",\n"
        "\t\ttp->t_pos.y, tp->t_pos.x,\n"
        "\t\ttp->t_stats.s_hpt, tp->t_stats.s_maxhp,\n"
        "\t\ttp->t_stats.s_lvl, tp->t_flags);\n"
        "\tif (tp->t_disguise && tp->t_disguise != tp->t_type)\n"
        '\t    fprintf(fp, " disguise=\'%c\'", tp->t_disguise);\n'
        '\tfprintf(fp, "\\n");\n'
        "    }\n"
        '    fprintf(fp, "Total monsters: %d\\n", monster_count);\n'
        "\n"
        "    /* ---- Level objects ---- */\n"
        '    fprintf(fp, "\\n--- Level Objects ---\\n");\n'
        "    object_count = 0;\n"
        "    for (tp = lvl_obj; tp != NULL; tp = next(tp)) {\n"
        "\tobject_count++;\n"
        '\tfprintf(fp, "  \'%c\' at (%d,%d): %s\\n",\n'
        "\t\ttp->o_type, tp->o_pos.y, tp->o_pos.x,\n"
        "\t\tinv_name(tp, FALSE));\n"
        "    }\n"
        '    fprintf(fp, "Total objects: %d\\n", object_count);\n'
        '    fprintf(fp, "Traps on level: %d\\n", ntraps);\n'
        "\n"
        "    /* ---- Screen buffer ---- */\n"
        '    fprintf(fp, "\\n--- Screen (places[] map) ---\\n");\n'
        "    /* Column header */\n"
        '    fprintf(fp, "    ");\n'
        "    for (x = 0; x < NUMCOLS; x++)\n"
        '\tfprintf(fp, "%d", x % 10);\n'
        '    fprintf(fp, "\\n");\n'
        "    for (y = 0; y < NUMLINES; y++) {\n"
        '\tfprintf(fp, "%2d: ", y);\n'
        "\tfor (x = 0; x < NUMCOLS; x++) {\n"
        "\t    char ch = chat(y, x);\n"
        "\t    fprintf(fp, \"%c\", ch ? ch : ' ');\n"
        "\t}\n"
        '\tfprintf(fp, "\\n");\n'
        "    }\n"
        "\n"
        "    /* ---- Flags overlay ---- */\n"
        '    fprintf(fp, "\\n--- Flags overlay (hex nibble) ---\\n");\n'
        "    for (y = 0; y < NUMLINES; y++) {\n"
        '\tfprintf(fp, "%2d: ", y);\n'
        "\tfor (x = 0; x < NUMCOLS; x++) {\n"
        "\t    unsigned char f = flat(y, x);\n"
        '\t    fprintf(fp, "%c", f ? "0123456789abcdef"[f & 0xf] : \'.\');\n'
        "\t}\n"
        '\tfprintf(fp, "\\n");\n'
        "    }\n"
        "\n"
        '    fprintf(fp, "\\n--- State ---\\n");\n'
        '    fprintf(fp, "running=%d  door_stop=%d  count=%d\\n",\n'
        "\t    running, door_stop, count);\n"
        '    fprintf(fp, "no_command=%d  no_move=%d\\n", no_command, no_move);\n'
        '    fprintf(fp, "seed=%d\\n", seed);\n'
        "\n"
        "    fclose(fp);\n"
        '    msg("game state dumped to /tmp/rogue.dump");\n'
        "}\n"
        "\n/*\n * command:",
    ),
    # 2. Add 'g' key binding before the 'otherwise' default case.
    (
        "\t\twhen '@':\n"
        "\t\t    stat_msg = TRUE;\n"
        "\t\t    status();\n"
        "\t\t    stat_msg = FALSE;\n"
        "\t\t    after = FALSE;\n"
        "\t\totherwise:",
        "\t\twhen '@':\n"
        "\t\t    stat_msg = TRUE;\n"
        "\t\t    status();\n"
        "\t\t    stat_msg = FALSE;\n"
        "\t\t    after = FALSE;\n"
        "\t\twhen 'g':\n"
        "\t\t    dump_game_state();\n"
        "\t\t    after = FALSE;\n"
        "\t\totherwise:",
    ),
]

MDPORT_C_PATCHES = [
    # md_gethomedir(): skip getpwuid, just use getenv("HOME")
    (
        "#else\n"
        "    char slash = '/';\n"
        "    struct passwd *pw;\n"
        "    pw = getpwuid(getuid());\n"
        "\n"
        "    h = pw->pw_dir;\n"
        "\n"
        '    if (strcmp(h,"/") == 0)\n'
        "        h = NULL;\n"
        "#endif\n",
        "#else\n"
        "    char slash = '/';\n"
        "#endif\n",
    ),
    # md_getshell(): skip getpwuid, just use env/default
    (
        "#else\n"
        '    char *def = "/bin/sh";\n'
        "    struct passwd *pw;\n"
        "    pw = getpwuid(getuid());\n"
        "    s = pw->pw_shell;\n"
        "#endif\n",
        "#else\n"
        '    char *def = "/bin/sh";\n'
        "#endif\n",
    ),
    # md_getrealname(): skip getpwuid, just format uid as string
    (
        "#if !defined(_WIN32) && !defined(DJGPP)\n"
        "    struct passwd *pp;\n",
        "#if 0 /* no getpwuid in WASTE guest */\n"
        "    struct passwd *pp;\n",
    ),
]


def ensure_sysroot(repo_root: Path, output_base: Path) -> Path:
    """Build or locate the WASTE application sysroot."""
    sysroot = output_base / "sysroot"
    if (sysroot / "bin" / "waste-wasm-clang").is_file():
        return sysroot
    coreutils_sysroot = repo_root / "build" / "coreutils" / "sysroot"
    if (coreutils_sysroot / "bin" / "waste-wasm-clang").is_file():
        return coreutils_sysroot
    build_sysroot = repo_root / "src" / "html-rt" / "tools" / "build-waste-sysroot.py"
    subprocess.run(
        [sys.executable, str(build_sysroot),
         "--repo-root", str(repo_root),
         "--output", str(sysroot)],
        check=True,
    )
    return sysroot


def ensure_ncurses(repo_root: Path) -> Path:
    """Build ncurses or return the existing build directory."""
    ncurses_output = repo_root / "build" / "ncurses"
    if (ncurses_output / "libncurses.so.wasm").is_file():
        return ncurses_output
    build_ncurses = repo_root / "src" / "html-rt" / "tools" / "build-ncurses.py"
    subprocess.run(
        [sys.executable, str(build_ncurses),
         "--repo-root", str(repo_root),
         "--output", str(ncurses_output)],
        check=True,
    )
    return ncurses_output


def prepare_sources(
    rogue_source: Path,
    build_dir: Path,
    repo_root: Path,
) -> Path:
    """Copy rogue sources into the build directory and apply patches."""
    src_dir = build_dir / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    for name in ROGUE_SOURCES:
        source = rogue_source / name
        if source.is_file():
            shutil.copyfile(source, src_dir / name)

    # Copy headers.
    for hdr in ["rogue.h", "extern.h", "score.h"]:
        source = rogue_source / hdr
        if source.is_file():
            shutil.copyfile(source, src_dir / hdr)

    # Write our config.h.
    (src_dir / "config.h").write_text(CONFIG_H, encoding="utf-8")

    # Write the bool-size fix header (force-included via -include).
    (src_dir / "rogue-bool-fix.h").write_text(ROGUE_BOOL_FIX_H, encoding="utf-8")

    # Apply source patches.
    for filename, patches in [("main.c", MAIN_C_PATCHES),
                               ("mdport.c", MDPORT_C_PATCHES),
                               ("command.c", COMMAND_C_PATCHES)]:
        filepath = src_dir / filename
        if filepath.is_file():
            text = filepath.read_text(encoding="utf-8")
            for old, new in patches:
                if old in text:
                    text = text.replace(old, new)
                    print(f"  patch: {filename}")
            filepath.write_text(text, encoding="utf-8")

    patch_file = repo_root / "submodules" / "rogue-waste.patch"
    if not patch_file.is_file():
        raise FileNotFoundError(f"missing Rogue patch: {patch_file}")
    subprocess.run(
        ["patch", "--batch", "--forward", "-p1", "-i", str(patch_file)],
        cwd=src_dir,
        check=True,
    )
    print(f"  patch: {patch_file.name}")

    return src_dir


def compile_rogue(
    src_dir: Path,
    build_dir: Path,
    sysroot: Path,
    ncurses_build: Path,
) -> list[Path]:
    """Compile rogue source files to wasm32 object files."""
    cc = str(sysroot / "bin" / "waste-wasm-clang")
    obj_dir = build_dir / "objects"
    obj_dir.mkdir(parents=True, exist_ok=True)

    # ncurses headers are in the sysroot (installed by build-ncurses.py)
    # and the configure-generated headers in ncurses build dir.
    include_flags = [
        f"-I{src_dir}",
        f"-I{ncurses_build / 'build' / 'include'}",
    ]

    defines = [
        "-DHAVE_CONFIG_H",
        "-DNDEBUG",
    ]

    # Force-include the bool fix so <stdbool.h> is short-circuited before
    # any source file's own includes run. This keeps `bool` = NCURSES_BOOL
    # (unsigned, 4 bytes) consistent across all translation units.
    bool_fix_flags = ["-include", str(src_dir / "rogue-bool-fix.h")]

    objects = []
    failed = []

    for name in ROGUE_SOURCES:
        source = src_dir / name
        if not source.is_file():
            failed.append((name, f"missing source: {source}"))
            continue
        obj = obj_dir / name.replace(".c", ".o")
        cmd = [cc, "-c", "-O2", "-fPIC"] + defines + bool_fix_flags + include_flags + [
            "-Wno-unused-parameter",
            "-Wno-sign-compare",
            "-Wno-implicit-function-declaration",
            "-Wno-int-conversion",
            "-Wno-dangling-else",
            "-Wno-sometimes-uninitialized",
            "-Wno-parentheses",
            str(source), "-o", str(obj),
        ]
        result = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            failed.append((name, result.stderr[:400]))
        else:
            objects.append(obj)

    crt_source = sysroot / "lib" / "waste-crt.c"
    crt_object = obj_dir / "waste-crt.o"
    if not crt_source.is_file():
        failed.append(("waste-crt.c", f"missing target CRT: {crt_source}"))
    else:
        result = subprocess.run(
            [cc, "-c", "-O2", "-fPIC", str(crt_source), "-o", str(crt_object)],
            capture_output=True, text=True, check=False,
        )
        if result.returncode != 0:
            failed.append(("waste-crt.c", result.stderr[:400]))
        else:
            objects.append(crt_object)

    if failed:
        print(f"\ncompile: {len(objects)} ok, {len(failed)} failed")
        for name, err in failed:
            print(f"  FAIL {name}: {err.strip()[:300]}")
        raise RuntimeError("Rogue compilation failed; refusing to link or stage a partial executable")
    else:
        print(f"compile: {len(objects)} objects ok")
    return objects


def link_executable(
    objects: list[Path],
    output: Path,
) -> None:
    """Link object files into a wasm32 executable."""
    command = [
        "wasm-ld",
        "--no-entry",
        "--import-memory",
        "--import-table",
        "--export=_start",
        "--export=main",
        "--export=__heap_base",
        "--export=__data_end",
        "--experimental-pic",
        "--unresolved-symbols=import-dynamic",
        # The resident env libc keeps its C static storage below page 5.
        # External-image data starts at page 5 so exec cannot overwrite
        # libc's FILE pointers and other process-local runtime state.
        "--global-base=327680",
        # Keep the executable's indirect functions above the resident Bash/
        # libc table and the dependency slots allocated below this boundary.
        "--table-base=1024",
        "-o", str(output),
    ] + [str(o) for o in objects]
    subprocess.run(command, check=True)
    size = output.stat().st_size
    print(f"link: {output.name} ({size} bytes)")


def get_ncurses_exports(lib_path: Path) -> set[str]:
    """Read the export section of libncurses.so.wasm and return function names."""
    data = lib_path.read_bytes()
    exports = set()
    pos = 8  # skip magic + version
    while pos < len(data):
        section_id = data[pos]; pos += 1
        size = 0; shift = 0
        while True:
            b = data[pos]; pos += 1
            size |= (b & 0x7f) << shift
            shift += 7
            if not (b & 0x80):
                break
        section_end = pos + size
        if section_id == 7:  # export section
            count = 0; shift = 0
            while True:
                b = data[pos]; pos += 1
                count |= (b & 0x7f) << shift
                shift += 7
                if not (b & 0x80):
                    break
            for _ in range(count):
                name_len = 0; shift = 0
                while True:
                    b = data[pos]; pos += 1
                    name_len |= (b & 0x7f) << shift
                    shift += 7
                    if not (b & 0x80):
                        break
                name = data[pos:pos + name_len].decode("utf-8", errors="replace")
                pos += name_len
                kind = data[pos]; pos += 1
                idx = 0; shift = 0
                while True:
                    b = data[pos]; pos += 1
                    idx |= (b & 0x7f) << shift
                    shift += 7
                    if not (b & 0x80):
                        break
                if kind == 0:  # function export
                    exports.add(name)
            break
        pos = section_end
    return exports


def read_leb128(data: bytes, pos: int) -> tuple[int, int]:
    """Read an unsigned LEB128 value, return (value, new_pos)."""
    value = 0; shift = 0
    while True:
        b = data[pos]; pos += 1
        value |= (b & 0x7f) << shift
        shift += 7
        if not (b & 0x80):
            break
    return value, pos


def encode_leb128(value: int) -> bytes:
    """Encode an unsigned LEB128 value."""
    result = bytearray()
    while True:
        byte = value & 0x7f
        value >>= 7
        if value:
            byte |= 0x80
        result.append(byte)
        if not value:
            break
    return bytes(result)


def rewrite_imports(
    wasm_path: Path,
    ncurses_exports: set[str],
    output_path: Path,
) -> int:
    """Rewrite module names for ncurses imports from "env" to "libncurses".

    Returns the number of imports rewritten.
    """
    data = bytearray(wasm_path.read_bytes())
    rewritten = 0

    # Find the import section (section id 2).
    pos = 8
    while pos < len(data):
        section_id = data[pos]
        section_start = pos
        pos += 1
        size, pos = read_leb128(bytes(data), pos)
        payload_start = pos
        section_end = pos + size

        if section_id == 2:  # import section
            # We need to rebuild the entire import section because changing
            # module name lengths shifts all offsets.
            count, ipos = read_leb128(bytes(data), pos)
            imports = []
            for _ in range(count):
                mod_len, ipos = read_leb128(bytes(data), ipos)
                mod_name = data[ipos:ipos + mod_len].decode("utf-8")
                ipos += mod_len
                name_len, ipos = read_leb128(bytes(data), ipos)
                import_name = data[ipos:ipos + name_len].decode("utf-8")
                ipos += name_len
                kind = data[ipos]; ipos += 1
                # Read the type descriptor (varies by kind).
                desc_start = ipos
                if kind == 0:  # function
                    _, ipos = read_leb128(bytes(data), ipos)
                elif kind == 1:  # table
                    _, ipos = read_leb128(bytes(data), ipos)  # elem type
                    flags, ipos = read_leb128(bytes(data), ipos)
                    _, ipos = read_leb128(bytes(data), ipos)  # min
                    if flags & 1:
                        _, ipos = read_leb128(bytes(data), ipos)  # max
                elif kind == 2:  # memory
                    flags, ipos = read_leb128(bytes(data), ipos)
                    _, ipos = read_leb128(bytes(data), ipos)  # min
                    if flags & 1:
                        _, ipos = read_leb128(bytes(data), ipos)  # max
                elif kind == 3:  # global
                    _, ipos = read_leb128(bytes(data), ipos)  # type
                    _, ipos = read_leb128(bytes(data), ipos)  # mutability
                desc = bytes(data[desc_start:ipos])

                # Rewrite: if module is "env" and the name is a ncurses export,
                # change module to "libncurses".
                new_mod = mod_name
                if mod_name == "env" and import_name in ncurses_exports:
                    new_mod = "libncurses"
                    rewritten += 1

                imports.append((new_mod, import_name, kind, desc))

            # Rebuild the import section payload.
            new_payload = bytearray()
            new_payload.extend(encode_leb128(count))
            for mod, name, kind, desc in imports:
                mod_bytes = mod.encode("utf-8")
                name_bytes = name.encode("utf-8")
                new_payload.extend(encode_leb128(len(mod_bytes)))
                new_payload.extend(mod_bytes)
                new_payload.extend(encode_leb128(len(name_bytes)))
                new_payload.extend(name_bytes)
                new_payload.append(kind)
                new_payload.extend(desc)

            # Rebuild the section: id + size + payload.
            new_section = bytearray()
            new_section.append(2)  # section id
            new_section.extend(encode_leb128(len(new_payload)))
            new_section.extend(new_payload)

            # Replace old section with new one.
            data = data[:section_start] + new_section + data[section_end:]
            break

        pos = section_end

    output_path.write_bytes(bytes(data))
    return rewritten


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path,
                        default=Path("build/rogue"))
    args = parser.parse_args()
    repo_root = args.repo_root.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    rogue_source = repo_root / "submodules" / "rogue"
    if not (rogue_source / "main.c").is_file():
        print(f"error: rogue source not found at {rogue_source}")
        return 1

    # Ensure ncurses is built first (provides headers + shared lib).
    ncurses_build = ensure_ncurses(repo_root)
    print(f"ncurses: {ncurses_build}")

    # Ensure sysroot.
    sysroot_candidates = [
        repo_root / "build" / "coreutils" / "sysroot",
        output / "sysroot",
    ]
    sysroot = None
    for s in sysroot_candidates:
        if (s / "bin" / "waste-wasm-clang").is_file():
            sysroot = s
            break
    if sysroot is None:
        sysroot = ensure_sysroot(repo_root, output)
    print(f"sysroot: {sysroot}")

    # Prepare sources (copy + patch).
    src_dir = prepare_sources(rogue_source, output, repo_root)
    print(f"sources: {src_dir}")

    # Compile.
    objects = compile_rogue(src_dir, output, sysroot, ncurses_build)
    if not objects:
        print("error: no objects compiled")
        return 1

    # Link (raw, before import rewriting).
    raw_output = output / "rogue-raw.wasm"
    link_executable(objects, raw_output)

    # Read ncurses export list.
    ncurses_lib = ncurses_build / "libncurses.so.wasm"
    ncurses_exports = get_ncurses_exports(ncurses_lib)
    print(f"ncurses exports: {len(ncurses_exports)} functions")

    # Rewrite imports: "env" -> "libncurses" for ncurses symbols.
    final_output = output / "rogue.wasm"
    rewritten = rewrite_imports(raw_output, ncurses_exports, final_output)
    size = final_output.stat().st_size
    print(f"import rewrite: {rewritten} imports moved env -> libncurses")
    print(f"output: {final_output.name} ({size} bytes)")

    # Stage for VFS.
    vfs_stage = output / "vfs"
    vfs_stage.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(final_output, vfs_stage / "rogue")
    print(f"\nstaged: {vfs_stage / 'rogue'}")
    print("VFS path: /usr/bin/rogue")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
