// Guest LLD location matching through the existing LLVM POSIX regex backend.
#ifndef WASTE_LLD_COMMON_DIAGNOSTICS_H
#define WASTE_LLD_COMMON_DIAGNOSTICS_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Regex.h"
#include <string>

namespace lld::detail {
inline std::string guestDiagnosticLocation(llvm::StringRef message,
                                           llvm::StringRef logName) {
  struct LocationPattern {
    llvm::Regex Pattern;
    unsigned File, Line;
  };
  // POSIX ERE has capturing optional groups and no \S/\d escapes. Record the
  // actual captures explicitly, and keep '.' from matching CR/newline as in
  // the native ECMAScript expressions. Static owners retain normal cleanup.
  static LocationPattern patterns[] = {
      {llvm::Regex("^undefined ([^[:space:]]+ )?symbol:[^\r\n]*\n"
                   ">>> referenced by [^\r\n]+\\(([^[:space:]]+):([0-9]+)\\)"),
       2, 3},
      {llvm::Regex("^undefined ([^[:space:]]+ )?symbol:[^\r\n]*\n"
                   ">>> referenced by ([^[:space:]]+):([0-9]+)"), 2, 3},
      {llvm::Regex("^undefined symbol:[^\r\n]*\n>>> referenced by ([^\r\n]*):"),
       1, 0},
      {llvm::Regex("^duplicate symbol: [^\r\n]*\n>>> defined in ([^[:space:]]+)\n"
                   ">>> defined in[^\r\n]*"), 1, 0},
      {llvm::Regex("^duplicate symbol: [^\r\n]*\n"
                   ">>> defined at [^\r\n]+\\(([^[:space:]]+):([0-9]+)\\)"),
       1, 2},
      {llvm::Regex("^duplicate symbol: [^\r\n]*\n"
                   ">>> defined at ([^[:space:]]+):([0-9]+)"), 1, 2},
      {llvm::Regex("[^\r\n]*\n>>> defined in [^\r\n]*\n"
                   ">>> referenced by [^\r\n]+\\(([^[:space:]]+):([0-9]+)\\)"),
       1, 2},
      {llvm::Regex("[^\r\n]*\n>>> defined in [^\r\n]*\n"
                   ">>> referenced by ([^[:space:]]+):([0-9]+)"), 1, 2},
      {llvm::Regex("([^[:space:]]+):([0-9]+): unclosed quote"), 1, 2},
  };
  for (auto &pattern : patterns) {
    llvm::SmallVector<llvm::StringRef, 4> matches;
    if (!pattern.Pattern.match(message, &matches))
      continue;
    std::string result = matches[pattern.File].str();
    if (pattern.Line)
      result += "(" + matches[pattern.Line].str() + ")";
    return result;
  }
  return logName.str();
}

inline bool guestSplitDuplicate(llvm::StringRef message,
                                 llvm::SmallVectorImpl<llvm::StringRef> &matches) {
  static llvm::Regex pattern(
      "^(duplicate symbol: [^\r\n]*)"
      "(\n>>> defined at [^[:space:]]+:[0-9]+[^\r\n]*\n>>>[^\r\n]*)"
      "(\n>>> defined at [^[:space:]]+:[0-9]+[^\r\n]*\n>>>[^\r\n]*)$");
  return pattern.match(message, &matches);
}
} // namespace lld::detail
#endif
