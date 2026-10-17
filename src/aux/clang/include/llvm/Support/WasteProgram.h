// Guest-only capability checks; process state remains in the shared C engine.
#ifndef LLVM_SUPPORT_WASTEPROGRAM_H
#define LLVM_SUPPORT_WASTEPROGRAM_H

#include "llvm/Support/Program.h"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <time.h>
#include <unistd.h>
#include <waste/process.h>

namespace llvm::detail {

// The guest can detach through setsid, but cannot enforce LLVM's optional
// memory or CPU-affinity requests. Validate before allocating or forking.
inline bool checkProgramExecutionCapabilities(
    ArrayRef<std::optional<StringRef>> Redirects, unsigned MemoryLimit,
    BitVector *AffinityMask, std::string *ErrMsg) {
  const char *Unavailable = nullptr;
  int Error = ENOSYS;
  if (!Redirects.empty() && Redirects.size() != 3) {
    Unavailable = "Guest process redirects require three entries";
    Error = EINVAL;
  } else if (MemoryLimit) {
    Unavailable = "Guest child memory limits are not supported";
  } else if (AffinityMask) {
    Unavailable = "Guest child CPU affinity is not supported";
  }
  if (!Unavailable)
    return true;
  if (ErrMsg)
    *ErrMsg = Unavailable;
  errno = Error;
  return false;
}

// Args already contains argv[0]. Program names a path, not another argument.
// Query the shared kernel and account for the current environment, rather than
// using a host ARG_MAX or reserving an arbitrary fraction for environment.
template <typename ArgType>
inline bool commandLineFitsGuestLimits(ArrayRef<ArgType> Args) {
  waste_exec_limits Limits;
  if (waste_get_exec_limits(&Limits) != 0 || Args.size() > Limits.max_args)
    return false;
  size_t Remaining = Limits.max_bytes;
  for (StringRef Arg : Args) {
    if (Arg.size() >= Remaining)
      return false;
    Remaining -= Arg.size() + 1;
  }
  size_t Count = 0;
  if (environ)
    for (char **Entry = environ; *Entry; ++Entry) {
      if (++Count > Limits.max_env)
        return false;
      size_t Length = ::strnlen(*Entry, Remaining);
      if (Length == Remaining)
        return false;
      Remaining -= Length + 1;
    }
  return true;
}

// Reject unavailable accounting/clocks before creating or reaping a child.
// Timed Wait requests one absolute deadline; retries keep that same operand.
inline bool checkProgramWaitCapabilities(
    std::optional<unsigned> SecondsToWait,
    std::optional<sys::ProcessStatistics> *ProcStat, std::string *ErrMsg,
    uint64_t *Deadline = nullptr) {
  const char *Unavailable = nullptr;
  int Error = ENOSYS;
  if (ProcStat) {
    ProcStat->reset();
    Unavailable = "Guest child statistics are not supported";
  } else if (SecondsToWait && *SecondsToWait != 0) {
    struct timespec Time;
    if (::clock_gettime(CLOCK_MONOTONIC, &Time) != 0) {
      Error = errno;
      Unavailable = "Guest monotonic child wait clock is not available";
    } else if (Deadline) {
      uint64_t Now = uint64_t(Time.tv_sec) * 1000000000 + Time.tv_nsec;
      uint64_t Duration = uint64_t(*SecondsToWait) * 1000000000;
      *Deadline = Now > UINT64_MAX - Duration ? UINT64_MAX : Now + Duration;
    }
  }
  if (!Unavailable)
    return true;
  if (ErrMsg)
    *ErrMsg = Unavailable;
  errno = Error;
  return false;
}

} // namespace llvm::detail
#endif
