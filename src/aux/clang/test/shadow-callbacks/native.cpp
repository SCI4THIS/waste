// Compile the complete production source with original native/threaded streams.
#include "llvm/Config/llvm-config.h"
#ifdef WASTE_TEST_NATIVE_THREADS
#undef LLVM_ENABLE_THREADS
#define LLVM_ENABLE_THREADS 1
#endif
#ifdef WASTE_TEST_GUEST_THREADS
#undef LLVM_ENABLE_THREADS
#define LLVM_ENABLE_THREADS 1
#undef LLVM_WASTE_GUEST
#define LLVM_WASTE_GUEST 1
#endif
#include WASTE_CHECK_SOURCE
