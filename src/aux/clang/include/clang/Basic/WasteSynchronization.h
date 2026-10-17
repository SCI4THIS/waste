// Compiler-local synchronization for LLVM's explicit disabled-thread profile.
#ifndef CLANG_BASIC_WASTE_SYNCHRONIZATION_H
#define CLANG_BASIC_WASTE_SYNCHRONIZATION_H

#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ExponentialBackoff.h"
#include "llvm/Support/Mutex.h"
#include <chrono>
#include <mutex>

namespace clang::detail {

using CompilerMutex = llvm::sys::SmartMutex<true>;

// There is one compiler execution thread. A pending unbounded wait cannot
// acquire a producer; reject it even in release builds. Timed waits use the
// existing monotonic backoff/POSIX sleep path, releasing ownership while the
// engine yields. Notifications need no queue: every retry checks the predicate.
class CompilerConditionVariable {
public:
  CompilerConditionVariable() = default;
  CompilerConditionVariable(const CompilerConditionVariable &) = delete;
  CompilerConditionVariable &operator=(const CompilerConditionVariable &) = delete;
  void notify_one() noexcept {}
  void notify_all() noexcept {}

  template <typename Lock, typename Predicate>
  void wait(Lock &, Predicate Ready) {
    if (!Ready())
      llvm::report_fatal_error("pending Clang wait in disabled-thread profile");
  }

  template <typename Lock, typename Rep, typename Period, typename Predicate>
  bool wait_for(Lock &Ownership, std::chrono::duration<Rep, Period> Timeout,
                Predicate Ready) {
    if (Ready())
      return true;
    llvm::ExponentialBackoff Backoff(
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(Timeout));
    for (;;) {
      Ownership.unlock();
      bool Retry = Backoff.waitForNextAttempt();
      Ownership.lock();
      if (Ready())
        return true;
      if (!Retry)
        return false;
    }
  }
};

} // namespace clang::detail
#endif
