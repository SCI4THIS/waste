// COFF-only guest command; the Windows executable is data produced by this tool.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "lld/Common/Driver.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"

LLD_HAS_DRIVER(coff)

// Freestanding C++ does not give main its hosted special linkage. The shared
// guest CRT calls the ordinary C symbol with this two-argument signature.
extern "C" int main(int argc, char **argv) {
  llvm::InitLLVM init(argc, argv);
  const lld::DriverDef drivers[] = {{lld::WinLink, &lld::coff::link}};
  return lld::lldMain({argv, static_cast<size_t>(argc)}, llvm::outs(),
                      llvm::errs(), drivers).retCode;
}
