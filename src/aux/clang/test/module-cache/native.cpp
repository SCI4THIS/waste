// Preserve the standard native/threaded types in the complete cache source.
#include "llvm/Config/llvm-config.h"
#ifdef WASTE_TEST_THREADED_BRANCH
#undef LLVM_ENABLE_THREADS
#define LLVM_ENABLE_THREADS 1
#define LLVM_WASTE_GUEST 1
#endif
#include "clang/DependencyScanning/InProcessModuleCache.h"
#ifdef WASTE_CHECK_SOURCE
#include WASTE_CHECK_SOURCE
#else
#include "InProcessModuleCache.cpp"
#endif
#include <type_traits>
static_assert(std::is_same<decltype(clang::dependencies::ModuleCacheEntry::Mutex), std::mutex>::value);
static_assert(std::is_same<decltype(clang::dependencies::ModuleCacheEntry::CondVar), std::condition_variable>::value);
