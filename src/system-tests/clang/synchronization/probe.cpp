// Observe real LLVM wrappers and C++ archives; WAST owns all expectations.
#include "llvm/Support/PerThreadBumpPtrAllocator.h"
#include "llvm/Support/RWMutex.h"
#include <cstring>
#include <type_traits>
#include <unistd.h>

struct alignas(64) ObserverAllocator : llvm::AllocatorBase<ObserverAllocator> {
  static unsigned constructed, destroyed;
  unsigned serial = ++constructed;
  unsigned resets = 0, red_zone = 0, size = 0, alignment = 0;
  unsigned deallocated_size = 0, deallocated_alignment = 0;
  const void *deallocated = nullptr;
  alignas(64) unsigned char bytes[64] = {};
  ~ObserverAllocator() { ++destroyed; }
  void *Allocate(size_t count, size_t align) {
    size = count; alignment = align;
    return bytes;
  }
  void Deallocate(const void *pointer, size_t count, size_t align) {
    deallocated = pointer; deallocated_size = count; deallocated_alignment = align;
  }
  void Reset() { ++resets; }
  size_t getTotalMemory() const { return sizeof(bytes); }
  void setRedZoneSize(size_t value) { red_zone = value; }
  using llvm::AllocatorBase<ObserverAllocator>::Allocate;
  using llvm::AllocatorBase<ObserverAllocator>::Deallocate;
};
unsigned ObserverAllocator::constructed = 0;
unsigned ObserverAllocator::destroyed = 0;
using Allocator = llvm::parallel::PerThreadAllocator<ObserverAllocator>;
struct IndexedAllocator : Allocator {
  unsigned id() const { return Id; }
};

struct report {
  unsigned metadata[10];
  unsigned rw[2][25];
  unsigned allocator[35];
};

template <bool mt_only> static void observe_rw(unsigned (&out)[25]) {
  llvm::sys::SmartRWMutex<mt_only> mutex;
  out[0] = mutex.try_lock_shared();
  out[1] = mutex.try_lock_shared();
  out[2] = mutex.try_lock();
  out[3] = mutex.unlock_shared();
  out[4] = mutex.try_lock();
  out[5] = mutex.unlock_shared();
  out[6] = mutex.try_lock();
  out[7] = mutex.try_lock();
  out[8] = mutex.try_lock_shared();
  out[9] = mutex.unlock();
  out[10] = mutex.lock_shared();
  out[11] = mutex.lock_shared();
  out[12] = mutex.unlock_shared();
  out[13] = mutex.unlock_shared();
  {
    llvm::sys::SmartScopedReader<mt_only> outer(mutex);
    out[14] = mutex.try_lock();
    llvm::sys::SmartScopedReader<mt_only> inner(mutex);
    out[15] = mutex.try_lock();
    out[16] = mutex.try_lock_shared();
    out[17] = mutex.unlock_shared();
  }
  out[18] = mutex.try_lock();
  out[19] = mutex.unlock();
  {
    llvm::sys::SmartScopedWriter<mt_only> writer(mutex);
    out[20] = mutex.try_lock_shared();
  }
  out[21] = mutex.try_lock_shared();
  out[22] = mutex.unlock_shared();
  out[23] = mutex.try_lock();
  out[24] = mutex.unlock();
}

static void observe_allocator(unsigned (&out)[35]) {
  unsigned starting_id = llvm::parallel::detail::claimPerThreadAllocatorId();
  {
    IndexedAllocator first, second;
    out[0] = first.id() > starting_id && second.id() > first.id();
    out[1] = ObserverAllocator::constructed;
    out[2] = first.getNumberOfAllocators();
    out[3] = second.getNumberOfAllocators();
    auto *a = static_cast<unsigned char *>(first.Allocate(16, 32));
    std::memset(a, 0x5a, 16);
    auto &cached = first.getThreadLocalAllocator();
    out[4] = ObserverAllocator::constructed;
    out[5] = first.getNumberOfAllocators();
    out[6] = &cached == &first.getThreadLocalAllocator();
    out[7] = cached.size;
    out[8] = cached.alignment;
    out[9] = reinterpret_cast<uintptr_t>(&cached) % alignof(ObserverAllocator);
    out[10] = reinterpret_cast<uintptr_t>(a) % 32;
    first.Deallocate(a, 16, 32);
    out[32] = cached.deallocated_size;
    out[33] = cached.deallocated_alignment;
    out[34] = cached.deallocated == a;
    auto *b = static_cast<unsigned char *>(second.Allocate(8, 16));
    std::memset(b, 0xa5, 8);
    out[11] = a != b;
    out[12] = a[0];
    out[13] = b[0];
    out[14] = ObserverAllocator::constructed;
    out[15] = second.getNumberOfAllocators();
    Allocator moved(std::move(first));
    out[16] = &moved.getThreadLocalAllocator() == &cached;
    out[17] = moved.getNumberOfAllocators();
    out[18] = ObserverAllocator::constructed;
    out[19] = ObserverAllocator::destroyed;
    moved.setRedZoneSize(7);
    moved.Reset();
    out[20] = cached.red_zone;
    out[21] = cached.resets;
    out[22] = moved.getTotalMemory();
    // Move assignment releases second's previous state and transfers the ID.
    static_cast<Allocator &>(second) = std::move(moved);
    out[23] = ObserverAllocator::destroyed;
    out[24] = &second.getThreadLocalAllocator() == &cached;
    out[25] = second.getNumberOfAllocators();
    unsigned previous = second.id();
    for (unsigned index = 0; index < 12; ++index) {
      IndexedAllocator temporary;
      // Expose ID/cache regressions as observations, not C++ assertions.
      if (temporary.id() > previous) ++out[26];
      previous = temporary.id();
      temporary.getThreadLocalAllocator();
    }
    out[27] = &second.getThreadLocalAllocator() == &cached && cached.bytes[0] == 0x5a;
    out[28] = ObserverAllocator::constructed;
    out[29] = ObserverAllocator::destroyed;
  }
  out[30] = ObserverAllocator::constructed;
  out[31] = ObserverAllocator::destroyed;
}

extern "C" int main(int argc, char **argv, char **environment) {
  (void)argc; (void)argv; (void)environment;
  report out = {};
  out.metadata[0] = LLVM_WASTE_GUEST;
  out.metadata[1] = LLVM_ENABLE_THREADS;
  out.metadata[2] = _LIBCPP_HAS_THREADS;
  out.metadata[3] = llvm::llvm_is_multithreaded();
  out.metadata[4] = std::is_copy_constructible<llvm::sys::RWMutex>::value;
  out.metadata[5] = std::is_move_constructible<llvm::sys::RWMutex>::value;
  out.metadata[6] = std::is_copy_constructible<llvm::sys::ScopedReader>::value;
  out.metadata[7] = std::is_move_constructible<llvm::sys::ScopedReader>::value;
  out.metadata[8] = std::is_copy_constructible<Allocator>::value;
  out.metadata[9] = std::is_move_constructible<Allocator>::value;
  observe_rw<false>(out.rw[0]);
  observe_rw<true>(out.rw[1]);
  observe_allocator(out.allocator);
  return write(1, &out, sizeof(out)) != (ssize_t)sizeof(out);
}
