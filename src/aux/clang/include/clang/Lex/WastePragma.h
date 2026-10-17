// The debug pragma uses the same absolute-deadline sleep as LLVM backoff.
#ifndef WASTE_CLANG_LEX_PRAGMA_H
#define WASTE_CLANG_LEX_PRAGMA_H
#include "llvm/Support/WasteBackoff.h"
#include <chrono>
namespace clang::detail {
inline void wastePragmaSleep() {
  llvm::detail::wasteBackoffSleepUntil(std::chrono::steady_clock::now() +
                                     std::chrono::milliseconds(100));
}
} // namespace clang::detail
#endif
