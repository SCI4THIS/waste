#!/usr/bin/env python3
"""Generate a self-contained C-engine Bash page.

Stages generated engine/bootstrap assets or packages the authored frontend
through build.sh. No generated copy of the shell UI or worker is maintained.

Terminal output is captured through the posix_write host import.  Interactive
input is copied into the engine-owned terminal kernel; a blocked read yields
through the native continuation and the worker resumes after input arrives.
No asyncify transform, no SharedArrayBuffer, works on file://.
"""

import argparse
import subprocess
from pathlib import Path
from runtime_config import javascript



def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate a self-contained C-engine Bash page"
    )
    parser.add_argument("--repo-root", type=Path,
                        default=Path(__file__).resolve().parents[3])
    parser.add_argument("--wasm", type=Path, required=True,
                        help="Path to waste-wast.wasm (C engine)")
    parser.add_argument("--launch", type=Path,
                        help="Legacy option; bootstrap comes from the current VFS tree")
    parser.add_argument("--vfs-root", type=Path, default=None,
                        help="Current VFS distribution tree")
    output = parser.add_mutually_exclusive_group(required=True)
    output.add_argument("--output", type=Path,
                        help="Self-contained HTML output using the authored frontend")
    output.add_argument("--output-dir", type=Path,
                        help="Generated staging directory under repository build/")
    args = parser.parse_args()

    args.repo_root = args.repo_root.resolve()
    if args.output_dir and not args.output_dir.resolve().is_relative_to(args.repo_root / "build"):
        parser.error("--output-dir must be under repository build/")

    if not args.wasm.is_file():
        raise SystemExit(f"C engine Wasm not found: {args.wasm}")
    vfs_root = args.vfs_root or args.repo_root / "src/vfs"
    subprocess.run(["python3", str(Path(__file__).with_name("vfs.py")),
                    "audit", "--root", str(vfs_root)], check=True)

    if args.output_dir:
        import os
        import shutil
        out_dir = args.output_dir
        out_dir.mkdir(parents=True, exist_ok=True)
        (out_dir / "runtime-config.js").write_text(javascript())

        def safe_copy(src: Path, dst: Path) -> None:
            """Copy src to dst, skipping if they resolve to the same file."""
            try:
                if os.path.samefile(src, dst):
                    return
            except OSError:
                pass
            shutil.copy2(src, dst)

        safe_copy(args.wasm, out_dir / "waste-wast.wasm")
        safe_copy(vfs_root / "usr/share/waste/launch.wast", out_dir / "launch.wast")
        subprocess.run(["python3", str(Path(__file__).with_name("vfs.py")),
                        "manifest", "--root", str(vfs_root),
                        "--output", str(out_dir / "vfs-manifest.json")], check=True)
        print(f"Generated bootstrap staging in {out_dir}; guest tree remains {vfs_root}")

    else:
        subprocess.run([
            "bash", str(Path(__file__).with_name("build.sh")), "bash",
            "--source-dir", str(args.repo_root / "src/html-rt/src"),
            "--vfs-root", str(vfs_root.resolve()),
            "--wasm", str(args.wasm.resolve()),
            "--output", str(args.output.resolve()),
        ], check=True)


if __name__ == "__main__":
    main()
