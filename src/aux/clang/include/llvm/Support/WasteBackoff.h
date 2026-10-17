// Guest-only backoff support; kernel waits remain in the runtime's POSIX ABI.
#ifndef LLVM_SUPPORT_WASTE_BACKOFF_H
#define LLVM_SUPPORT_WASTE_BACKOFF_H

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <sys/random.h>
#include <time.h>

namespace llvm::detail {

// The current libc provider supplies process-local compatibility PRNG bytes,
// not operating-system entropy. They are used only for non-security jitter.
class WasteBackoffRandom {
public:
  WasteBackoffRandom() = default;
  WasteBackoffRandom(const WasteBackoffRandom &) = delete;
  WasteBackoffRandom &operator=(const WasteBackoffRandom &) = delete;
  using result_type = uint32_t;
  static constexpr result_type min() { return 0; }
  static constexpr result_type max() {
    return std::numeric_limits<result_type>::max();
  }
  result_type operator()() {
    result_type Value;
    if (getrandom(&Value, sizeof(Value), 0) !=
        static_cast<ssize_t>(sizeof(Value)))
      std::abort();
    return Value;
  }
};

inline void wasteBackoffSleepUntil(std::chrono::steady_clock::time_point End) {
  using namespace std::chrono;
  for (;;) {
    auto Now = steady_clock::now();
    if (Now >= End)
      return;
    auto Remaining = duration_cast<nanoseconds>(End - Now).count();
    timespec Request = {Remaining / 1000000000,
                        static_cast<long>(Remaining % 1000000000)};
    if (nanosleep(&Request, nullptr) == 0)
      return;
    if (errno != EINTR)
      std::abort();
    // Recompute from the clock: the current libc's interrupted nanosleep does
    // not provide an accurate remaining interval. Do not restart a full delay.
  }
}

} // namespace llvm::detail
#endif
