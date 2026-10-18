// Compile the full actual source, retaining native/threaded blocking fields.
#include "llvm/Config/llvm-config.h"
#ifdef WASTE_TEST_THREADED_BRANCH
#undef LLVM_ENABLE_THREADS
#define LLVM_ENABLE_THREADS 1
#define LLVM_WASTE_GUEST 1
#endif
#include "Parallel.cpp"
static_assert(sizeof(llvm::parallel::detail::Latch) > sizeof(std::atomic<uint32_t>));
