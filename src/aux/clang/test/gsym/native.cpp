// Compile the complete native and forced threaded-guest source branches.
#include "llvm/Config/llvm-config.h"
#ifdef WASTE_TEST_THREADED_BRANCH
#undef LLVM_ENABLE_THREADS
#define LLVM_ENABLE_THREADS 1
#define LLVM_WASTE_GUEST 1
#endif
#ifdef WASTE_CHECK_SOURCE
#include WASTE_CHECK_SOURCE
#else
#include "DwarfTransformer.cpp"
#endif
