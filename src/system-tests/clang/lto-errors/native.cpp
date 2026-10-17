// Check actual owner and complete source in native and threaded guest profiles.
#include "llvm/Config/llvm-config.h"
#ifdef WASTE_TEST_THREADED_BRANCH
#undef LLVM_ENABLE_THREADS
#define LLVM_ENABLE_THREADS 1
#define LLVM_WASTE_GUEST 1
#endif
#include "llvm/LTO/LTO.h"
#include "llvm/ProfileData/SampleProf.h"
#include <type_traits>
struct NativeOwner : llvm::lto::ThinBackendProc {
  using MutexType = decltype(ErrMu);
};
static_assert(std::is_same<NativeOwner::MutexType, std::mutex>::value);
#if LLVM_ENABLE_THREADS
static_assert(std::is_same<llvm::DefaultThreadPool, llvm::StdThreadPool>::value);
#else
static_assert(std::is_same<llvm::DefaultThreadPool, llvm::SingleThreadExecutor>::value);
#endif
#ifdef WASTE_CHECK_SOURCE
#include WASTE_CHECK_SOURCE
#endif
