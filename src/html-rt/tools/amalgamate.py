#!/usr/bin/env python3
"""Amalgamate staging source files into a single self-contained HTML.

Takes the index.html template and inlines all external CSS, JS, and data
references to produce a file:// compatible single HTML page.

Usage:
  python3 amalgamate.py --page-dir DIR --tarball-js FILE --loader-js FILE \
      --manifest-tar-gz FILE --zlibaux-wasm FILE --output FILE
"""

import argparse
import base64
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Amalgamate HTML page")
    parser.add_argument("--page-dir", type=Path, required=True,
                        help="Staging page directory (contains index.html, style.css, app.js)")
    parser.add_argument("--tarball-js", type=Path, required=True,
                        help="Path to tarball.js")
    parser.add_argument("--loader-js", type=Path, required=True,
                        help="Path to loader.js")
    parser.add_argument("--manifest-tar-gz", type=Path, required=True,
                        help="Path to compressed manifest.tar.gz")
    parser.add_argument("--zlibaux-wasm", type=Path, required=True,
                        help="Path to zlibaux.wasm")
    parser.add_argument("--output", type=Path, required=True,
                        help="Output HTML file")
    parser.add_argument("--chunk-size", type=int, default=1048576,
                        help="Chunk size for base64 splitting (default: 1MB)")
    args = parser.parse_args()

    page_dir = args.page_dir
    index_html = page_dir / "index.html"
    style_css = page_dir / "style.css"
    app_js = page_dir / "app.js"

    for f in [index_html, style_css, app_js, args.tarball_js,
              args.loader_js, args.manifest_tar_gz, args.zlibaux_wasm]:
        if not f.is_file():
            print(f"error: required file not found: {f}", file=sys.stderr)
            return 1

    html = index_html.read_text(encoding="utf-8")

    # 1. Inline style.css
    css = style_css.read_text(encoding="utf-8")
    html = html.replace(
        '<link rel="stylesheet" href="style.css">',
        "<style>\n" + css + "\n  </style>"
    )

    # 2. Inline tarball.js
    tarball_js = args.tarball_js.read_text(encoding="utf-8")
    html = html.replace(
        '<script src="../../../../submodules/tarballjs/tarball.js"></script>',
        "<script>\n" + tarball_js + "\n</script>"
    )

    # 3. Inline loader.js
    loader_js = args.loader_js.read_text(encoding="utf-8")
    html = html.replace(
        '<script src="../shared/loader.js"></script>',
        "<script>\n" + loader_js + "\n</script>"
    )

    # 4. Inline app.js
    app = app_js.read_text(encoding="utf-8")
    html = html.replace(
        '<script src="app.js"></script>',
        "<script>\n" + app + "\n</script>"
    )

    # 5. Split manifest.tar.gz into chunks and base64 encode as data URIs
    tar_data = args.manifest_tar_gz.read_bytes()
    chunk_size = args.chunk_size
    chunks = []
    for i in range(0, len(tar_data), chunk_size):
        chunk = tar_data[i:i + chunk_size]
        b64 = base64.b64encode(chunk).decode("ascii")
        chunks.append(f'"data:application/octet-stream;base64,{b64}"')

    manifest_urls = ",\n".join(chunks)
    html = html.replace('"manifest.tar.gz"', manifest_urls)
    print(f"  Manifest: {len(tar_data):,} bytes -> {len(chunks)} chunk(s)")

    # 6. Base64 encode zlibaux.wasm as data URI
    zlibaux_data = args.zlibaux_wasm.read_bytes()
    zlibaux_b64 = base64.b64encode(zlibaux_data).decode("ascii")
    zlibaux_uri = f"data:application/wasm;base64,{zlibaux_b64}"
    html = html.replace('"zlibaux.wasm.b64"', f'"{zlibaux_uri}"')
    print(f"  zlibaux.wasm: {len(zlibaux_data):,} bytes ({len(zlibaux_b64):,} chars base64)")

    # 7. Flip is_staging to false
    html = html.replace("is_staging: true", "is_staging: false")

    # Write output
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(html, encoding="utf-8")
    print(f"  Output: {args.output} ({args.output.stat().st_size:,} bytes)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
