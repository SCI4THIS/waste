// Capture actual C++ provider calls and staged LLVM filesystem observations.
// All expected results and functional assertions live in platform.wast.
#include "llvm/Support/FileSystem.h"
#include <cerrno>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

static int observations[128];
static unsigned count;
static void record(int value) { observations[count++] = value; }

static void filesystem_queries() {
  for (const char *path : {"/tmp", "/tmp/platform-does-not-exist"}) {
    errno = 73;
    auto space = llvm::sys::fs::disk_space(path);
    record(!space);
    record(space ? 0 : space.getError().value());
    record(!space && space.getError().category() == std::generic_category());
    record(errno);
    bool result = true;
    record(llvm::sys::fs::is_local(path, result).value());
    record(result);
    record(errno);
    result = false;
    record(llvm::sys::fs::is_local(path, result).value());
    record(result);
    record(llvm::sys::fs::is_local(path));
  }
  int fd = open("/tmp/platform-file", O_CREAT | O_TRUNC | O_RDWR, 0600);
  record(fd >= 0);
  for (int descriptor : {fd, -1}) {
    errno = 73;
    bool result = true;
    record(llvm::sys::fs::is_local(descriptor, result).value());
    record(result);
    record(errno);
    record(llvm::sys::fs::is_local(descriptor));
  }
  record(close(fd));
}

static void mappings() {
  constexpr size_t length = 65536;
  auto *memory = static_cast<unsigned char *>(
      mmap(nullptr, length, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  record(memory != MAP_FAILED);
  if (memory != MAP_FAILED) {
    record(memory[0]);
    record(memory[length - 1]);
    memory[0] = 0x42;
    record(mprotect(memory, length, PROT_READ));
    record(memory[0]);
    record(mprotect(memory, length, PROT_READ | PROT_WRITE));
    memory[length - 1] = 0xab;
    record(memory[length - 1]);
    record(munmap(memory, length));
  }
  errno = 0;
  record(mmap(nullptr, 0, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED);
  record(errno);
  errno = 0;
  record(mmap(nullptr, length, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0) == MAP_FAILED);
  record(errno);

  int fd = open("/tmp/platform-file", O_TRUNC | O_RDWR, 0600);
  record(fd >= 0);
  record(write(fd, "abcd", 4));
  auto *shared = static_cast<unsigned char *>(
      mmap(nullptr, 4, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  record(shared != MAP_FAILED);
  if (shared != MAP_FAILED) {
    record(shared[0]);
    shared[0] = 'Z';
    record(msync(shared, 4, MS_SYNC));
    char bytes[4] = {};
    record(lseek(fd, 0, SEEK_SET));
    record(read(fd, bytes, sizeof(bytes)));
    record(bytes[0]);
    auto *private_copy = static_cast<unsigned char *>(
        mmap(nullptr, 4, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0));
    record(private_copy != MAP_FAILED);
    if (private_copy != MAP_FAILED) {
      private_copy[0] = 'Q';
      record(msync(private_copy, 4, MS_SYNC));
      record(lseek(fd, 0, SEEK_SET));
      record(read(fd, bytes, sizeof(bytes)));
      record(bytes[0]);
      record(munmap(private_copy, 4));
    }
    record(munmap(shared, 4));
  }
  // If the C++ import loses the i64 width, this offset becomes zero and succeeds.
  errno = 0;
  record(mmap(nullptr, 4, PROT_READ, MAP_PRIVATE, fd, INT64_C(4294967296)) == MAP_FAILED);
  record(errno);
  record(close(fd));
}

static void libraries() {
  record(dlerror() == nullptr);
  void *first = dlopen("/tmp/platform-library.so.wasm", RTLD_NOW | RTLD_LOCAL);
  record(first != nullptr);
  if (first) {
    using Function = int (*)(int);
    auto function = reinterpret_cast<Function>(dlsym(first, "platform_add"));
    record(function != nullptr);
    if (function)
      record(function(7));
    void *second = dlopen("/tmp/platform-library.so.wasm", RTLD_NOW | RTLD_LOCAL);
    record(second == first);
    record(dlsym(second, "platform_add") == reinterpret_cast<void *>(function));
    record(dlclose(first));
    if (function)
      record(function(2));
    record(dlsym(second, "platform_missing") == nullptr);
    record(dlerror() != nullptr);
    record(dlerror() == nullptr);
    record(dlclose(second));
  }
  record(dlopen("/tmp/platform-missing.so.wasm", RTLD_NOW) == nullptr);
  record(dlerror() != nullptr);
  record(dlerror() == nullptr);
  // Process-wide lookup is a known provider limitation, not LLVM acceptance.
  record(dlopen(nullptr, RTLD_NOW) == nullptr);
  record(dlerror() != nullptr);
  record(dlerror() == nullptr);
}

extern "C" int main(int, char **, char **) {
  filesystem_queries();
  mappings();
  libraries();
  observations[127] = static_cast<int>(count);
  write(STDOUT_FILENO, observations, sizeof(observations));
  return 0;
}
