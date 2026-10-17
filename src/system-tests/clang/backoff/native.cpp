// Compile the native and guest/threaded branches with the host C++ library.
#include "llvm/Config/llvm-config.h"
#ifdef WASTE_TEST_THREADED_BRANCH
#undef LLVM_ENABLE_THREADS
#define LLVM_ENABLE_THREADS 1
#define LLVM_WASTE_GUEST 1
#endif
#include "llvm/Support/ExponentialBackoff.h"

void instantiate_native_backoff() {
  llvm::ExponentialBackoff Backoff(std::chrono::milliseconds(10));
  (void)Backoff.waitForNextAttempt();
}
