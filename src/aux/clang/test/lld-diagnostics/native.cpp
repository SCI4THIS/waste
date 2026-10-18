// Compile the complete standard and forced threaded-guest diagnostic branches.
#include "llvm/Config/llvm-config.h"
#ifdef WASTE_TEST_THREADED_BRANCH
#undef LLVM_ENABLE_THREADS
#define LLVM_ENABLE_THREADS 1
#define LLVM_WASTE_GUEST 1
#endif
#include "ErrorHandler.cpp"
