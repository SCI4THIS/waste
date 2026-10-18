// Observe the production module task owner, real LLVM tasks and cache locks.
// All expectations, including callback order and lifetime, belong to WAST.
#include "ModuleCompileTasks.h"
#include "clang/Basic/AtomicLineLogger.h"
#include "clang/DependencyScanning/InProcessModuleCache.h"
#include "llvm/Support/AdvisoryLock.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/MemoryBuffer.h"
#include <signal.h>
#include <time.h>
#include <unistd.h>

using clang::dependencies::detail::AsyncModuleCompiles;
using namespace clang;
using namespace clang::dependencies;
static_assert(!std::is_copy_constructible<AsyncModuleCompiles>::value);
static_assert(!std::is_move_constructible<AsyncModuleCompiles>::value);
static_assert(!std::is_copy_constructible<llvm::unique_function<void()>>::value);
static void record(int64_t value) { write(1, &value, sizeof(value)); }
static unsigned Live, Calls;
static volatile sig_atomic_t Signals;
static void handler(int) { ++Signals; }

struct Owner {
  unsigned Id;
  AsyncModuleCompiles *OnDestroy = nullptr;
  explicit Owner(unsigned id) : Id(id) { ++Live; }
  ~Owner() {
    record(100 + Id);
    --Live;
    if (OnDestroy)
      OnDestroy->add([] { ++Calls; record(901); record(Live); });
  }
};

static void recurse(AsyncModuleCompiles &group, unsigned depth) {
  group.add([&, depth, token = std::make_unique<Owner>(20 + depth)] {
    ++Calls;
    record(200 + depth);
    record(token->Id);
    record(Live);
    if (depth) recurse(group, depth - 1);
    record(300 + depth);
  });
}

static void attempt(llvm::AdvisoryLock &lock) {
  auto result = lock.tryLock();
  record(bool(result));
  if (result) record(*result);
  else { llvm::consumeError(result.takeError()); record(-1); }
}

extern "C" int main(int, char **, char **) {
  record(LLVM_WASTE_GUEST); record(LLVM_ENABLE_THREADS); record(_LIBCPP_HAS_THREADS);
  { AsyncModuleCompiles empty; }
  record(Live); record(Calls);
  {
    AsyncModuleCompiles group;
    record(10);
    group.add([&, token = std::make_unique<Owner>(1)] {
      ++Calls;
      record(token->Id); record(Live); record(20);
      group.add([nested = std::make_unique<Owner>(2)] {
        ++Calls;
        record(nested->Id); record(Live); record(30);
      });
      record(Live); record(40);
    });
    record(Live); record(50); record(Calls);
  }
  record(Live); record(Calls);
  {
    AsyncModuleCompiles group;
    recurse(group, 3);
    record(Live); record(Calls);
  }
  record(Live); record(Calls);
  {
    AsyncModuleCompiles group;
    auto token = std::make_unique<Owner>(90);
    token->OnDestroy = &group;
    group.add([token = std::move(token)] { ++Calls; record(900); record(Live); });
    record(Live); record(Calls);
  }
  record(Live); record(Calls);

  // Actual advisory owners refer to service entries, not the temporary cache.
  AtomicLineLogger logger;
  ModuleCacheEntries entries;
  AsyncModuleCompiles group;
  auto cache = makeInProcessModuleCache(entries, logger);
  auto lock = cache->getLock("/tmp/task-parent.pcm");
  attempt(*lock);
  auto contender = cache->getLock("/tmp/task-parent.pcm");
  cache.reset();
  record(entries.Map["/tmp/task-parent.pcm"]->Locked);
  std::string source = "owning compiler invocation and controller capture";
  llvm::unique_function<void()> compile =
      [&, lock = std::move(lock), data = source, compiler = std::make_unique<Owner>(3),
       diagnostic = std::make_unique<Owner>(4), controller = std::make_unique<Owner>(5)] {
    ++Calls;
    record(data == "owning compiler invocation and controller capture");
    record(compiler->Id); record(diagnostic->Id); record(controller->Id);
    record(Live); attempt(*contender);
    llvm::CrashRecoveryContext recovery;
    record(recovery.RunSafely([&] {
      auto nested_cache = makeInProcessModuleCache(entries, logger);
      auto nested_lock = nested_cache->getLock("/tmp/task-child.pcm");
      attempt(*nested_lock);
      nested_cache.reset();
      group.add([&, child_lock = std::move(nested_lock), child = std::make_unique<Owner>(6)] {
        ++Calls;
        record(child->Id); record(Live);
        record(entries.Map["/tmp/task-parent.pcm"]->Locked);
        record(entries.Map["/tmp/task-child.pcm"]->Locked);
        record(signal(SIGUSR1, handler) == SIG_ERR);
        struct timespec before = {}, after = {};
        record(clock_gettime(CLOCK_MONOTONIC, &before));
        const char marker[] = "__MODULE_TASK_SLEEP__\n";
        write(2, marker, sizeof(marker) - 1);
        // This is the same yielding POSIX sleep used by sleep/backoff, not an
        // LLVM scheduler or host thread. Captures must survive interruption.
        struct timespec request = {0, 10000000}, remaining = {};
        record(nanosleep(&request, &remaining));
        record(Signals);
        record(entries.Map["/tmp/task-parent.pcm"]->Locked);
        record(entries.Map["/tmp/task-child.pcm"]->Locked);
        record(Live);
        record(nanosleep(&request, nullptr));
        record(clock_gettime(CLOCK_MONOTONIC, &after));
        record((after.tv_sec - before.tv_sec) * 1000000000LL + after.tv_nsec - before.tv_nsec);
        record(child->Id);
      });
      record(entries.Map["/tmp/task-child.pcm"]->Locked);
      record(entries.Map["/tmp/task-parent.pcm"]->Locked);
      record(Live);
    }));
    record(recovery.RetCode);
    record(Calls);
  };
  source.assign("mutated source");
  group.add(std::move(compile));
  record(bool(compile));
  record(Live); record(Calls);
  record(entries.Map["/tmp/task-parent.pcm"]->Locked);
  record(entries.Map["/tmp/task-child.pcm"]->Locked);
  attempt(*contender); contender.reset();
  record(entries.Map["/tmp/task-parent.pcm"]->Locked);
  // A fresh owner after all callbacks and teardown must remain usable.
  { AsyncModuleCompiles again; again.add([] { ++Calls; record(1000); }); }
  record(Live); record(Calls);
  return 19;
}
