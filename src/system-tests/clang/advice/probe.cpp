// Observe actual LLVM mapping methods and public C++ calls; WAST owns expectations.
#include "llvm/Support/FileSystem.h"
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <type_traits>
#include <unistd.h>
#include <utility>

static_assert(std::is_same_v<decltype(&posix_madvise), int (*)(void *, size_t, int)>);
void observe_ranges();

extern "C" int main(int, char **, char **) {
  using llvm::sys::fs::mapped_file_region;
  int values[40] = {}, index = 0;
  auto record = [&](int value) { values[index++] = value; };
  record(POSIX_MADV_NORMAL);
  record(POSIX_MADV_RANDOM);
  record(POSIX_MADV_SEQUENTIAL);
  record(POSIX_MADV_WILLNEED);
  record(POSIX_MADV_DONTNEED);
  const char *path = "/tmp/advice-cxx-file";
  int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
  record(fd >= 0);
  record(ftruncate(fd, 65537));
  record(write(fd, "LLVM", 4));
  std::error_code error;
  {
    mapped_file_region mapping(fd, mapped_file_region::readonly, 65537, 0, error);
    record(error.value());
    record(bool(mapping));
    if (mapping) {
      record(mapping.size());
      record(close(fd));
      record(unlink(path));
      errno = 81;
      mapping.dontNeed();
      record(errno);
      record(mapping.const_data()[0]);
      record(mapping.const_data()[65536]);
      mapping.willNeed();
      record(errno);
      record(mapping.const_data()[1]);
      mapping.randomAccess();
      record(errno);
      record(mapping.const_data()[3]);
      record(posix_madvise(mapping.data(), mapping.size(), POSIX_MADV_SEQUENTIAL));
      record(errno);
      record(posix_madvise(mapping.data(), mapping.size(), -1));
      record(errno);
      mapped_file_region moved(std::move(mapping));
      record(bool(mapping));
      record(bool(moved));
      mapping.dontNeed();
      mapping.willNeed();
      mapping.randomAccess();
      record(errno);
      record(moved.const_data()[2]);
      void *old = moved.data();
      moved.unmap();
      record(bool(moved));
      record(posix_madvise(old, 1, POSIX_MADV_NORMAL));
      record(errno);
    }
  }
  errno = 82;
  mapped_file_region failed(-1, mapped_file_region::readonly, 1, 0, error);
  record(bool(failed));
  record(error.value());
  record(errno);
  failed.dontNeed();
  failed.willNeed();
  failed.randomAccess();
  record(errno);
  values[39] = index;
  write(STDOUT_FILENO, values, sizeof(values));
  observe_ranges();
  return 0;
}
