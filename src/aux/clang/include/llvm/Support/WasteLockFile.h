// Guest lock-owner queries use the engine's process table, never host PIDs.
#ifndef LLVM_SUPPORT_WASTE_LOCK_FILE_H
#define LLVM_SUPPORT_WASTE_LOCK_FILE_H

#include "llvm/ADT/StringRef.h"
#include <cerrno>
#include <signal.h>
#include <unistd.h>

namespace llvm::detail {

inline bool wasteLockOwnerStillExecuting(StringRef OwnerHost, int PID) {
  char HostName[256] = {};
  if (gethostname(HostName, sizeof(HostName) - 1) != 0 || !HostName[0])
    return true; // A failed identity lookup cannot justify removing a lock.
  if (StringRef(HostName) != OwnerHost)
    return true; // Another host's PIDs are outside this process namespace.
  if (PID < 0)
    return false; // Invalid owner; never reinterpret it as a process group.
  if (PID == 0)
    return true; // Preserve getsid(0)'s current-process behavior.
  return !(kill(PID, 0) == -1 && errno == ESRCH);
}

} // namespace llvm::detail
#endif
