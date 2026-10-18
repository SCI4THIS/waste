// Compile the complete job owner with the native/threaded statistics paths.
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
