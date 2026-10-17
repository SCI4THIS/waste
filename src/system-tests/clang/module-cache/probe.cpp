// Observe actual Clang cache sources and Support; expectations belong to WAST.
#include "clang/Basic/AtomicLineLogger.h"
#include "clang/Basic/CompilerSynchronization.h"
#include "clang/DependencyScanning/InProcessModuleCache.h"
#include "clang/Serialization/InMemoryModuleCache.h"
#include "llvm/Support/AdvisoryLock.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace clang;
using namespace clang::dependencies;
using namespace std::chrono;
static_assert(!std::is_copy_constructible<clang::detail::CompilerMutex>::value);
static_assert(!std::is_copy_constructible<clang::detail::CompilerConditionVariable>::value);
static void record(int64_t value) { write(1, &value, sizeof(value)); }
static void marker(const char *text) { write(2, text, __builtin_strlen(text)); }
static volatile sig_atomic_t Signals;
static void handler(int) { ++Signals; }
static void save(const char *path, llvm::StringRef data) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  record(fd >= 0);
  record(fd < 0 ? -1 : write(fd, data.data(), data.size()));
  record(fd < 0 ? -1 : close(fd));
}
static void attempt(llvm::AdvisoryLock &lock) {
  auto result = lock.tryLock();
  record(bool(result));
  if (result) record(*result);
  else { llvm::consumeError(result.takeError()); record(-1); }
}
static void read(ModuleCache &cache, llvm::StringRef path,
                 llvm::StringRef expected) {
  off_t size = -1; time_t time = -1;
  auto buffer = cache.read(path, size, time);
  record(bool(buffer));
  if (buffer) record((*buffer)->getBuffer() == expected);
  else { llvm::consumeError(buffer.takeError()); record(-1); }
  record(size); record(time);
}

extern "C" int main(int, char **, char **) {
  record(LLVM_WASTE_GUEST); record(LLVM_ENABLE_THREADS); record(_LIBCPP_HAS_THREADS);
  AtomicLineLogger logger;
  ModuleCacheEntries entries;
  auto cache = makeInProcessModuleCache(entries, logger);
  record(cache->isDirectoryInvalidated("/tmp/cache").has_value());
  entries.ValidateAgainstInvalidatedPaths = true;
  record(*cache->isDirectoryInvalidated("/tmp/cache"));
  std::string path = "/tmp/cache";
  entries.addInvalidatedPath(path); path.assign("/tmp/changed");
  record(*cache->isDirectoryInvalidated("/tmp/cache"));
  record(*cache->isDirectoryInvalidated("/tmp/changed"));
  entries.addInvalidatedPath("/tmp/cache");
  record(*cache->isDirectoryInvalidated("/tmp/cache"));

  auto one = cache->getLock("/tmp/locks.pcm");
  auto two = cache->getLock("/tmp/locks.pcm");
  attempt(*one); attempt(*two);
  record(int(two->waitForUnlockFor(0s)));
  one.reset(); record(int(two->waitForUnlockFor(0s))); attempt(*two);
  record(two->unsafeUnlock().value());
  auto three = cache->getLock("/tmp/locks.pcm"); attempt(*three);
  two.reset();
  record(entries.Map["/tmp/locks.pcm"]->Locked);
  record(entries.Map["/tmp/locks.pcm"]->Generation);
  auto four = cache->getLock("/tmp/locks.pcm"); attempt(*four);
  three.reset(); record(entries.Map["/tmp/locks.pcm"]->Locked);
  record(int(four->waitForUnlockFor(-1s)));
  record(cache->getModuleTimestamp("/tmp/locks.pcm"));
  cache->updateModuleTimestamp("/tmp/locks.pcm");
  record(cache->getModuleTimestamp("/tmp/locks.pcm"));

  char bytes[] = {'p', 'c', 'm', 0, 'a', '\n'};
  llvm::StringRef content(bytes, sizeof(bytes));
  off_t size = -1; time_t time = -1;
  record(cache->write("/tmp/written.pcm", llvm::MemoryBufferRef(content, "input"), size, time).value());
  record(size); record(time);
  record(entries.Map["/tmp/written.pcm"]->State);
  record(entries.Map["/tmp/written.pcm"]->WrittenBuffer->getBufferStart() != bytes);
  bytes[0] = 'X'; read(*cache, "/tmp/written.pcm", llvm::StringRef("pcm\0a\n", 6));
  content = llvm::StringRef("pcm\0a\n", 6);
  size = -1; time = -1;
  record(cache->write("/tmp/written.pcm", llvm::MemoryBufferRef(content, "again"), size, time).value());
  record(size); record(time);
  auto other = makeInProcessModuleCache(entries, logger);
  read(*other, "/tmp/written.pcm", content);
  entries.flush();
  auto disk = llvm::MemoryBuffer::getFile("/tmp/written.pcm");
  record(bool(disk)); record(disk && (*disk)->getBuffer() == content);

  save("/tmp/read.pcm", "original"); read(*cache, "/tmp/read.pcm", "original");
  save("/tmp/read.pcm", "changed!"); read(*cache, "/tmp/read.pcm", "original");
  record(entries.Map["/tmp/read.pcm"]->State);
  record(cache->write("/tmp/read.pcm", llvm::MemoryBufferRef("new", "input"), size, time).value());
  read(*cache, "/tmp/read.pcm", "new");
  auto missing = cache->read("/tmp/absent.pcm", size, time);
  record(bool(missing));
  record(missing ? -1 : llvm::errorToErrorCode(missing.takeError()).value());
  record(entries.Map["/tmp/absent.pcm"]->State);

  auto &memory = cache->getInMemoryModuleCache();
  record(memory.getPCMState("A"));
  auto &stored = memory.addPCM("A", llvm::MemoryBuffer::getMemBufferCopy("owned"), 5, 99);
  record(memory.getPCMState("A"));
  size = -1; time = -1;
  record(memory.lookupPCM("A", size, time) == &stored); record(size); record(time);
  record(memory.tryToDropPCM("A")); record(memory.getPCMState("A"));
  record(memory.shouldBuildPCM("A"));
  memory.addBuiltPCM("A", llvm::MemoryBuffer::getMemBufferCopy("rebuilt"), 7, 123);
  record(memory.getPCMState("A")); record(memory.isPCMFinal("A"));
  record(memory.tryToDropPCM("A"));
  memory.addPCM("B", llvm::MemoryBuffer::getMemBufferCopy("second"), 6, 456);
  memory.finalizePCM("B"); record(memory.getPCMState("B"));
  record(memory.lookupPCM("B", size, time)->getBuffer() == "second");
  record(size); record(time);

  struct sigaction action = {};
  action.sa_handler = handler; sigemptyset(&action.sa_mask);
  record(sigaction(SIGUSR1, &action, nullptr));
  auto owner = cache->getLock("/tmp/timed.pcm"); attempt(*owner);
  auto waiter = cache->getLock("/tmp/timed.pcm");
  auto start = steady_clock::now();
  marker("__MODULE_CACHE_TIMEOUT__\n");
  record(int(waiter->waitForUnlockFor(1s)));
  record(duration_cast<nanoseconds>(steady_clock::now() - start).count());
  record(entries.Map["/tmp/timed.pcm"]->Locked); record(Signals);
  owner.reset(); record(int(waiter->waitForUnlockFor(1s)));

  clang::detail::CompilerMutex mutex;
  clang::detail::CompilerConditionVariable condition;
  std::unique_lock ownership(mutex);
  condition.wait(ownership, [] { return true; }); record(ownership.owns_lock());
  record(condition.wait_for(ownership, 0s, [] { return true; }));
  record(condition.wait_for(ownership, -1s, [] { return false; }));
  record(ownership.owns_lock());
  Signals = 0; start = steady_clock::now();
  marker("__MODULE_CACHE_NOTIFY__\n");
  record(condition.wait_for(ownership, 1s, [] { return Signals != 0; }));
  record(duration_cast<nanoseconds>(steady_clock::now() - start).count());
  record(Signals); record(ownership.owns_lock());
  ownership.unlock();
  pid_t child = fork();
  if (child == 0) {
    std::unique_lock child_ownership(mutex);
    condition.wait(child_ownership, [] { return false; });
    _exit(19);
  }
  record(child > 0);
  int status = -1;
  record(child > 0 && waitpid(child, &status, 0) == child); record(status);

  // Exercise the real directory iterator retained by the pruning implementation.
  // Record only observed entries; WAST decides which files must be present.
  std::error_code error;
  unsigned seen = 0;
  llvm::sys::fs::directory_iterator end;
  for (llvm::sys::fs::directory_iterator iterator("/tmp", error);
       !error && iterator != end; iterator.increment(error)) {
    auto name = llvm::sys::path::filename(iterator->path());
    if (name == "written.pcm") seen |= 1;
    if (name == "read.pcm") seen |= 2;
  }
  record(error.value()); record(seen);
  llvm::sys::fs::directory_iterator missing_directory("/tmp/missing-cache-dir", error);
  record(error.value()); record(missing_directory == end);
  return 17;
}
