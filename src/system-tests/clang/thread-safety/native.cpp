// Compile a full production owner with the original native/threaded adapter.
#include "llvm/Config/llvm-config.h"
#if defined(WASTE_TEST_NATIVE_THREADS) || defined(WASTE_TEST_GUEST_THREADS)
#undef LLVM_ENABLE_THREADS
#define LLVM_ENABLE_THREADS 1
#endif
#ifdef WASTE_TEST_GUEST_THREADS
#undef LLVM_WASTE_GUEST
#define LLVM_WASTE_GUEST 1
#endif
#include WASTE_CHECK_SOURCE

// Also require the original StringRef adapter to remain callable in these
// profiles. The guest's production printer already uses llvm::raw_ostream.
std::ostream &waste_check_thread_safety_stream(std::ostream &Stream,
                                             llvm::StringRef Text) {
  return clang::threadSafety::operator<<(Stream, Text);
}
