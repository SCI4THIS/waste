// Compile the other audited synchronization declarations with real headers.
#include "llvm/Support/BalancedPartitioning.h"
#include "llvm/Support/FileCollector.h"
#include "llvm/Support/HashingOutputBackend.h"
#include "llvm/Support/PerThreadBumpPtrAllocator.h"
#include "llvm/Support/SHA256.h"
#include "clang/Basic/AtomicLineLogger.h"

void instantiate_hash_backend() {
  llvm::vfs::HashingOutputBackend<llvm::SHA256> backend;
  (void)backend.getHashValueForFile("missing");
}

void *instantiate_bump_allocator(llvm::parallel::PerThreadBumpPtrAllocator &allocator) {
  return allocator.Allocate(24, 32);
}
