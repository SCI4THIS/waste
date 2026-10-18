// Record process-VM calls; functional expectations live exclusively in ranges.wast.
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <initializer_list>
#include <sys/mman.h>
#include <unistd.h>

extern "C" int legacy_advice(void *, size_t, int)
    __attribute__((import_module("env"), import_name("posix_madvise")));
extern "C" int libc_advice(void *, size_t, int)
    __attribute__((import_module("libc"), import_name("posix_madvise")));

void observe_ranges() {
  int values[256] = {}, index = 0;
  auto record = [&](int value) { values[index++] = value; };
  auto hint = [&](void *address, size_t length, int advice) {
    errno = 71;
    record(libc_advice(address, length, advice));
    record(errno);
  };
  auto legacy = [&](void *address, size_t length, int advice) {
    errno = 71;
    record(legacy_advice(address, length, advice));
    record(errno);
  };
  auto *base = static_cast<unsigned char *>(
      mmap(nullptr, 196608, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  record(base != MAP_FAILED);
  if (base != MAP_FAILED) {
    base[0] = 17;
    base[65535] = 35;
    base[65536] = 36;
    base[196607] = 51;
    for (int advice : {POSIX_MADV_NORMAL, POSIX_MADV_RANDOM, POSIX_MADV_SEQUENTIAL,
                       POSIX_MADV_WILLNEED, POSIX_MADV_DONTNEED}) {
      for (size_t length : {1u, 65537u, 196608u}) hint(base, length, advice);
      for (size_t offset : {0u, 65535u, 65536u, 196607u}) record(base[offset]);
      legacy(base, 196608, advice);
    }
    for (int advice : {-1, 5, 2147483647}) {
      hint(base, 1, advice);
      record(base[0]);
    }
    hint(base, 0, POSIX_MADV_NORMAL);
    record(base[0]);
    hint(base + 1, 1, POSIX_MADV_NORMAL);
    record(base[0]);
    hint(base, SIZE_MAX, POSIX_MADV_NORMAL);
    record(base[0]);
    hint(reinterpret_cast<void *>(uintptr_t(0x80000000)), 1, POSIX_MADV_NORMAL);
    hint(reinterpret_cast<void *>(uintptr_t(0xffff0000)), 131072, POSIX_MADV_NORMAL);
    hint(nullptr, 1, POSIX_MADV_NORMAL); // Initial process memory is mapped too.
    legacy(base, 196608, -1);
    record(mprotect(base, 65536, PROT_READ));
    hint(base, 65537, POSIX_MADV_DONTNEED);
    record(base[0]);
    record(mprotect(base, 65536, PROT_NONE));
    hint(base, 65537, POSIX_MADV_WILLNEED);
    record(mprotect(base, 65536, PROT_READ | PROT_WRITE));
    record(base[0]);
    record(munmap(base + 65536, 1));
    hint(base, 196608, POSIX_MADV_DONTNEED);
    hint(base, 65537, POSIX_MADV_WILLNEED);
    hint(base + 65536, 1, POSIX_MADV_NORMAL);
    hint(base, 65536, POSIX_MADV_DONTNEED);
    hint(base + 131072, 65536, POSIX_MADV_DONTNEED);
    record(base[0]);
    record(base[196607]);
    void *middle = mmap(base + 65536, 65536, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    record(middle == base + 65536);
    hint(base, 196608, POSIX_MADV_DONTNEED);
    if (middle != MAP_FAILED) record(base[65536]);
    record(munmap(base, 196608));
    hint(base, 1, POSIX_MADV_NORMAL);
  }
  const char *path = "/tmp/advice-range-file";
  int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
  record(fd >= 0);
  record(ftruncate(fd, 65537));
  record(write(fd, "A", 1));
  auto *private_map = static_cast<unsigned char *>(
      mmap(nullptr, 65537, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0));
  auto *shared = static_cast<unsigned char *>(
      mmap(nullptr, 65537, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  auto *alias = static_cast<unsigned char *>(
      mmap(nullptr, 65537, PROT_READ, MAP_SHARED, fd, 0));
  record(private_map != MAP_FAILED);
  record(shared != MAP_FAILED);
  record(alias != MAP_FAILED);
  if (private_map != MAP_FAILED && shared != MAP_FAILED && alias != MAP_FAILED) {
    auto disk = [&]() {
      unsigned char byte = 0;
      record(lseek(fd, 0, SEEK_SET));
      record(read(fd, &byte, 1));
      record(byte);
    };
    record(private_map[0]);
    record(shared[0]);
    record(alias[0]);
    private_map[0] = 80;
    hint(private_map, 65537, POSIX_MADV_DONTNEED);
    record(private_map[0]);
    record(shared[0]);
    disk();
    shared[0] = 83;
    hint(shared, 65537, POSIX_MADV_DONTNEED);
    record(private_map[0]);
    record(shared[0]);
    record(alias[0]);
    disk();
    record(msync(shared, 65537, MS_SYNC));
    disk();
    record(ftruncate(fd, 0));
    // Advice validates mappings without accessing now-truncated file pages.
    hint(shared, 65537, POSIX_MADV_WILLNEED);
    hint(private_map, 65537, POSIX_MADV_DONTNEED);
    record(ftruncate(fd, 65537));
    record(close(fd));
    record(unlink(path));
    hint(private_map, 65537, POSIX_MADV_SEQUENTIAL);
    hint(shared, 65537, POSIX_MADV_RANDOM);
    record(munmap(private_map, 65537));
    record(munmap(shared, 65537));
    record(munmap(alias, 65537));
  }
  values[255] = index;
  write(STDOUT_FILENO, values, sizeof(values));
}
