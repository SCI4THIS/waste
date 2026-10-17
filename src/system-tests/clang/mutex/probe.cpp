// Compile upstream LLVM locks and the real bootstrap libc++ call_once.
// This observer records results; all functional expectations live in WAST.
#include "llvm/Support/Mutex.h"
#include <type_traits>
#include <unistd.h>

struct report {
  unsigned metadata[8];
  unsigned mutex[2][12];
  unsigned once[6];
};

template <bool mt_only> void observe(unsigned (&values)[12]) {
  llvm::sys::SmartMutex<mt_only> mutex;
  values[0] = mutex.lock();
  values[1] = mutex.lock();
  values[2] = mutex.try_lock();
  values[3] = mutex.unlock();
  values[4] = mutex.unlock();
  values[5] = mutex.unlock();
  unsigned scoped = 0;
  {
    llvm::sys::SmartScopedLock<mt_only> outer(mutex);
    std::lock_guard<llvm::sys::SmartMutex<mt_only>> inner(mutex);
    scoped = 17;
    values[6] = mutex.try_lock();
    values[7] = mutex.unlock();
  }
  values[8] = scoped;
  {
    std::unique_lock<llvm::sys::SmartMutex<mt_only>> deferred(mutex, std::defer_lock);
    values[9] = deferred.owns_lock();
    values[10] = deferred.try_lock();
    values[11] = deferred.owns_lock();
  }
  mutex.lock();
  std::lock_guard<llvm::sys::SmartMutex<mt_only>> adopted(mutex, std::adopt_lock);
}

extern "C" int main(int argc, char **argv, char **environment) {
  (void)argc; (void)argv; (void)environment;
  report output = {};
  output.metadata[0] = LLVM_WASTE_GUEST;
  output.metadata[1] = LLVM_ENABLE_THREADS;
  output.metadata[2] = _LIBCPP_HAS_THREADS;
  output.metadata[3] = llvm::llvm_is_multithreaded();
  output.metadata[4] = std::is_copy_constructible<llvm::sys::Mutex>::value;
  output.metadata[5] = std::is_copy_assignable<llvm::sys::Mutex>::value;
  output.metadata[6] = std::is_move_constructible<llvm::sys::SmartMutex<true>>::value;
#ifdef NDEBUG
  output.metadata[7] = 0;
#else
  output.metadata[7] = 1;
#endif
  observe<false>(output.mutex[0]);
  observe<true>(output.mutex[1]);
  llvm::once_flag first, second;
  unsigned calls = 0, value = 0;
  auto initialize = [&](unsigned number) { ++calls; value = number; };
  llvm::call_once(first, initialize, 42U);
  output.once[0] = calls; output.once[1] = value;
  llvm::call_once(first, initialize, 99U);
  output.once[2] = calls; output.once[3] = value;
  llvm::call_once(second, initialize, 77U);
  output.once[4] = calls; output.once[5] = value;
  return write(1, &output, sizeof(output)) != (ssize_t)sizeof(output);
}
