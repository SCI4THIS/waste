#!/usr/bin/env python3
"""Keep the legacy adapter visible beside explicitly namespaced libc calls."""
from pathlib import Path
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[5] / "src/aux/libc/tools"))
from shared_libc import inspect, rewrite

source, provider, output = map(Path, sys.argv[1:])
count = rewrite(source, provider, output, kernel_functions=("posix_madvise",))
imports = inspect(output)["imports"]
signature = inspect(provider)["exports"]["posix_madvise"]
for namespace in ("env", "libc"):
    if imports.get(namespace + ":posix_madvise") != signature:
        raise SystemExit(f"{namespace}.posix_madvise import/signature missing")
print(f"Assigned {count} shared libc imports; retained env.posix_madvise")
