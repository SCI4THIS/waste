#!/usr/bin/env python3
"""Exercise the shared configuration reader without a C compiler or browser."""
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src/html-rt/tools"))
from runtime_config import read_config

with tempfile.TemporaryDirectory(prefix="waste-config-") as directory:
    path = Path(directory) / "config.h"
    path.write_text("""#ifndef EXAMPLE_CONFIG_H
#define EXAMPLE_CONFIG_H
/* References may precede their definitions. */
#define TOTAL (ENTRIES + RESERVE)
#define ENTRIES 2049u
#define RESERVE (3 * 11)
#define BYTES (129UL * 1024 * 1024) // C integer suffix
#endif
""")
    assert read_config(path) == dict(TOTAL=2082, ENTRIES=2049, RESERVE=33,
                                     BYTES=135266304)
    for invalid in (
        "#define A B\n#define B A\n",
        "#define A MISSING\n",
        "#define A 1\n#define A 2\n",
        "#define A 0\n",
        "#define A (1 - 2)\n",
        "#define A __import__('os')\n",
    ):
        path.write_text(invalid)
        try:
            read_config(path)
        except ValueError:
            pass
        else:
            raise AssertionError(f"invalid configuration accepted: {invalid}")
print("PASS shared config: expressions, references, C suffixes, malformed input rejection")
