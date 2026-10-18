// Compile the complete native and forced threaded-guest worker branches.
#include "llvm/Config/llvm-config.h"
#ifdef WASTE_TEST_THREADED_BRANCH
#undef LLVM_ENABLE_THREADS
#define LLVM_ENABLE_THREADS 1
#define LLVM_WASTE_GUEST 1
#endif
#include "DependencyScanningWorker.cpp"
#include <type_traits>
static_assert(!std::is_copy_constructible<clang::dependencies::detail::AsyncModuleCompiles>::value);
static_assert(!std::is_move_constructible<clang::dependencies::detail::AsyncModuleCompiles>::value);
