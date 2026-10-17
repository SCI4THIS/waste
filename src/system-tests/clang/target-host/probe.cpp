// Observe LLVM's actual guest host-target API; expectations are in target-host.wast.
#include "llvm/TargetParser/Host.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <cstddef>
#include <string>
#include <unistd.h>

extern "C" int main(int, char **, char **) {
  std::string output = "default=" + llvm::sys::getDefaultTargetTriple() + '\n';
  output += "process=" + llvm::sys::getProcessTriple() + '\n';
  output += "cpu=" + llvm::sys::getHostCPUName().str() + '\n';
  output += "features=";
  output += llvm::sys::getHostCPUFeatures().empty() ? "empty\n" : "nonempty\n";
  for (size_t offset = 0; offset < output.size();) {
    ssize_t count = write(1, output.data() + offset, output.size() - offset);
    if (count <= 0)
      return 19;
    offset += static_cast<size_t>(count);
  }
  return 23;
}
