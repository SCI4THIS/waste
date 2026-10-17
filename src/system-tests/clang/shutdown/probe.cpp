// Real static registrations and LLVM cache lifetime; WAST owns expectations.
#include "llvm/Support/PerThreadBumpPtrAllocator.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

extern "C" int __cxa_atexit(void (*)(void *), void *, void *);
extern "C" void __cxa_finalize(void *);
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
static bool CaptureAllocation;
static void *CacheAllocation;
static size_t CacheBytes, CacheExpectedBytes;
static unsigned CacheFreed, AllocatorsAlive, GlobalDestroyed;
extern "C" void *__wrap_malloc(size_t size) {
  void *pointer = __real_malloc(size);
  if (CaptureAllocation && !CacheAllocation) { CacheAllocation = pointer; CacheBytes = size; }
  return pointer;
}
extern "C" void __wrap_free(void *pointer) {
  if (pointer && pointer == CacheAllocation) ++CacheFreed;
  __real_free(pointer);
}

static void record(uint32_t value) { write(1, &value, sizeof(value)); }
struct Lifetime {
  unsigned Id;
  std::string Text;
  explicit Lifetime(unsigned id) : Id(id), Text(80, char('a' + id)) { record(100 + Id); }
  ~Lifetime() { ++GlobalDestroyed; record(200 + Id); record(Text.size()); record(Text[0]); }
};
static Lifetime First(1), Second(2), Third(3);
static Lifetime &local() { static Lifetime object(4); return object; }
static void plain_first() { record(301); }
static void plain_late() { record(303); record(usleep(1000)); }
static void plain_second() { record(302); record(atexit(plain_late)); }
static int Alpha, Beta;
static uint32_t Words[] = {501, 502, 503, 504, 505, 601};
static void word(void *argument) { record(*static_cast<uint32_t *>(argument)); }
static void recursive(void *argument) {
  word(argument);
  __cxa_finalize(&Alpha);
  record(__cxa_atexit(word, &Words[4], &Alpha));
}
static void group_plain() { record(350); }

struct CacheAllocator : llvm::AllocatorBase<CacheAllocator> {
  uint32_t Storage = 71;
  CacheAllocator() { ++AllocatorsAlive; }
  ~CacheAllocator() { --AllocatorsAlive; }
  void *Allocate(size_t, size_t) { return &Storage; }
};
using Cache = llvm::parallel::PerThreadAllocator<CacheAllocator>;
struct IndexedCache : Cache { unsigned id() const { return Id; } };
static void cache_report() {
  record(450); record(CacheFreed); record(CacheBytes == CacheExpectedBytes);
  record(AllocatorsAlive); record(GlobalDestroyed);
}

extern "C" int main(int argc, char **argv, char **) {
  if (argc != 2) return 99;
  if (std::string(argv[1]) == "groups") {
    record(10);
    record(__cxa_atexit(word, &Words[0], &Alpha));
    record(__cxa_atexit(word, &Words[5], &Beta));
    record(__cxa_atexit(word, &Words[1], &Alpha));
    __cxa_finalize(&Alpha); record(11);
    __cxa_finalize(&Alpha); record(12);
    record(__cxa_atexit(recursive, &Words[2], &Alpha));
    record(__cxa_atexit(word, &Words[3], &Alpha));
    __cxa_finalize(&Alpha); record(13);
    __cxa_finalize(&Beta); record(14);
    errno = 0; record(__cxa_atexit(nullptr, nullptr, &Alpha)); record(errno);
    errno = 0; record(atexit(nullptr)); record(errno);
    uint32_t many[64];
    int result = 0;
    for (unsigned i = 0; i < 64; ++i) {
      many[i] = 700 + i;
      result |= __cxa_atexit(word, &many[i], &Beta);
    }
    record(result); __cxa_finalize(&Beta); record(15);
    record(atexit(group_plain));
    return 44;
  }
  if (std::string(argv[1]) == "cache") {
    record(20); record(atexit(cache_report));
    {
      IndexedCache allocator;
      CacheExpectedBytes = (allocator.id() + 1) * sizeof(CacheAllocator *);
      CaptureAllocation = true;
      auto &cached = allocator.getThreadLocalAllocator();
      CaptureAllocation = false;
      record(CacheAllocation != nullptr); record(CacheBytes == CacheExpectedBytes);
      record(cached.Storage); record(&cached == &allocator.getThreadLocalAllocator());
      record(allocator.getNumberOfAllocators()); record(AllocatorsAlive); record(CacheFreed);
    }
    record(AllocatorsAlive); record(CacheFreed);
    return 45;
  }
  record(30); record(atexit(plain_first));
  record(local().Id); record(atexit(plain_second));
  if (std::string(argv[1]) == "exit") exit(29);
  if (std::string(argv[1]) == "immediate") _Exit(31);
  if (std::string(argv[1]) == "abort") abort();
  if (std::string(argv[1]) == "exec-fail") {
    char name[] = "/tmp/shutdown-invalid.wasm";
    int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) return 98;
    write(fd, "invalid", 7); close(fd);
    char *arguments[] = {name, nullptr};
    record(execve(name, arguments, nullptr)); record(errno);
    return 24;
  }
  if (std::string(argv[1]) == "exec") {
    char name[] = "/usr/bin/true";
    char *arguments[] = {name, nullptr};
    execve(name, arguments, nullptr);
    record(999); return 99;
  }
  if (std::string(argv[1]) == "fork") {
    pid_t child = fork();
    if (!child) {
      int fd = open("/tmp/shutdown-child.bin", O_WRONLY | O_CREAT | O_TRUNC, 0600);
      if (fd < 0 || dup2(fd, 1) < 0) _Exit(98);
      close(fd); record(402); return 41;
    }
    record(child > 0);
    int status = 0;
    record(waitpid(child, &status, 0) == child); record(status); record(GlobalDestroyed);
    return 43;
  }
  return 23;
}
