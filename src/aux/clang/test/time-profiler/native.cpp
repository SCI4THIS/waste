// Compile actual source and retain the native/threaded registry mutex.
#include "llvm/Config/llvm-config.h"
#ifdef WASTE_TEST_THREADED_BRANCH
#undef LLVM_ENABLE_THREADS
#define LLVM_ENABLE_THREADS 1
#define LLVM_WASTE_GUEST 1
#endif
#include "TimeProfiler.cpp"
#include <type_traits>
static_assert(std::is_same<TimeTraceMutex, std::mutex>::value);
