#!/usr/bin/env python3
"""Isolate actual LLVM ordinary signal/file methods, without fault symbolication."""
import hashlib
import json
from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
common = source / "llvm/lib/Support/Signals.cpp"
unix = source / "llvm/lib/Support/Unix/Signals.inc"


def section(contents, begin, end):
    if contents.count(begin) != 1 or contents.count(end) != 1:
        raise SystemExit("LLVM signal boundaries changed; re-audit the fixture")
    start, stop = contents.index(begin), contents.index(end)
    if start >= stop:
        raise SystemExit("LLVM signal boundary order changed; re-audit the fixture")
    return contents[start:stop]


core = section(common.read_text(), "struct CallbackAndCookie {",
               "// Signal-safe.\nstatic void insertSignalHandler(")
methods = section(unix.read_text(), "#if defined(LLVM_WASTE_GUEST)\n// The shared guest kernel",
                  "/// Add a function to be called when a signal is delivered to the process.")
license_text = common.read_text().split('#include', 1)[0]
generated = license_text + '''// Diagnostic fixture; production keeps the complete Signals.cpp source.
#include "llvm/Config/config.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/IOSandbox.h"
#include "llvm/Support/ExitCodes.h"
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace llvm;
''' + core + methods + '''
// Observation bridge to the real private utility registration path. The
// fixture does not replace LLVM's callback insertion or fatal-error provider.
extern "C" void signals_fixture_register_utility() { RegisterHandlers(true); }
'''
output.parent.mkdir(parents=True, exist_ok=True)
if not output.exists() or output.read_text() != generated:
    output.write_text(generated)
output.with_suffix('.json').write_text(json.dumps({
    "scope": "Actual LLVM ordinary signal registration/dispatch, file cleanup and RunSignalHandlers; no callback insertion, crash recovery or symbolication",
    "sources": {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (common, unix)},
    "generatedSha256": hashlib.sha256(generated.encode()).hexdigest(),
}, indent=2) + '\n')
