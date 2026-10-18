// Compile only: preserve the standard shared-future interface on both branches.
#include "llvm/Config/llvm-config.h"
#ifdef WASTE_TEST_THREADED_BRANCH
#undef LLVM_ENABLE_THREADS
#define LLVM_ENABLE_THREADS 1
#define LLVM_WASTE_GUEST 1
#endif
#include "llvm/Support/ThreadPool.h"
#include <type_traits>

static_assert(std::is_same<llvm::ThreadPoolFuture<int>, std::shared_future<int>>::value);
static_assert(std::is_same<llvm::ThreadPoolFuture<void>, std::shared_future<void>>::value);

void instantiate_native_tasks(llvm::ThreadPoolInterface &Pool) {
  std::shared_future<int> Value = Pool.async([] { return 17; });
  std::shared_future<void> Void = Pool.async([] {});
  Value.wait();
  Void.get();
  llvm::ThreadPoolTaskGroup Group(Pool);
  std::shared_future<int> Bound = Group.async([](int N) { return N + 1; }, 23);
  Bound.get();
}
