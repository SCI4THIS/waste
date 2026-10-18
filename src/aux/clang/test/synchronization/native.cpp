// Compile only: verify that native and threaded branches retain std backends.
#include "llvm/Config/llvm-config.h"
#ifdef WASTE_TEST_THREADED_BRANCH
// Exercise the guest/threaded preprocessor branch with the host's real std
// library. This is not a guest configure result or pthread capability claim.
#undef LLVM_ENABLE_THREADS
#define LLVM_ENABLE_THREADS 1
#define LLVM_WASTE_GUEST 1
#endif
#include "llvm/Support/FileCollector.h"
#include "llvm/Support/PerThreadBumpPtrAllocator.h"
#include "llvm/Support/RWMutex.h"
#include <type_traits>

struct CollectorTypes : llvm::FileCollectorBase {
  using lock_type = decltype(Mutex);
};
struct AllocatorTypes : llvm::parallel::PerThreadBumpPtrAllocator {
  using lock_type = decltype(S->Mutex);
};
static_assert(std::is_same<CollectorTypes::lock_type, std::mutex>::value);
static_assert(std::is_same<AllocatorTypes::lock_type, std::mutex>::value);
static_assert(!std::is_copy_constructible<llvm::sys::RWMutex>::value);
static_assert(!std::is_move_constructible<llvm::sys::RWMutex>::value);

void instantiate_native_locks(llvm::sys::RWMutex &mutex) {
  {
    llvm::sys::ScopedReader reader(mutex);
  }
  {
    llvm::sys::ScopedWriter writer(mutex);
  }
}
