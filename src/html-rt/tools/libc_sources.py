"""Guest libc sources shared by the PIC library and static test builders."""

from pathlib import Path
import re

from guest_sdk import include_flags


SOURCE_MANIFEST = Path(__file__).resolve().parents[2] / "libc/sources.mk"


def read_sources() -> tuple[str, ...]:
    text = re.sub(r"(?m)#.*$", "", SOURCE_MANIFEST.read_text()).replace("\\\n", " ")
    match = re.fullmatch(r"\s*LIBC_C_SOURCES\s*:=\s*(.*?)\s*", text, re.DOTALL)
    if not match:
        raise ValueError(f"unsupported libc source manifest: {SOURCE_MANIFEST}")
    sources = tuple(match[1].split())
    if (not sources or len(set(sources)) != len(sources)
            or any(not re.fullmatch(r"(?:[a-z_]+/)*[a-z_]+\.c", name) for name in sources)):
        raise ValueError(f"invalid libc source names: {SOURCE_MANIFEST}")
    return sources


LIBC_C_SOURCES = read_sources()


def libc_source_paths(root: Path) -> tuple[Path, ...]:
    lib_dir = root / "src" / "libc"
    return tuple(lib_dir / name for name in LIBC_C_SOURCES)


def guest_include_flags(root: Path) -> list[str]:
    return include_flags(root / "src/vfs")
