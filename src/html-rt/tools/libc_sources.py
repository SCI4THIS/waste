"""Shared guest-libc source manifest for the HTML runtime builders."""

from pathlib import Path

from guest_sdk import include_flags


LIBC_C_SOURCES = (
    "stdio.c",
    "wchar.c",
    "locale.c",
    "identity.c",
    "string.c",
    "math.c",
    "quad.c",
    "stdlib.c",
    "pattern.c",
    "misc.c",
    "time.c",
    "termcap.c",
    "termios.c",
    "dirent.c",
    "netdb.c",
    "dlfcn.c",
    "unistd.c",
    "sys/random.c",
    "sys/resource.c",
    "sys/select.c",
    "sys/ioctl.c",
    "sys/socket.c",
)


def libc_source_paths(root: Path) -> tuple[Path, ...]:
    lib_dir = root / "src" / "html-rt" / "lib"
    return tuple(lib_dir / name for name in LIBC_C_SOURCES)


def guest_include_flags(root: Path) -> list[str]:
    return include_flags(root / "src/vfs")
