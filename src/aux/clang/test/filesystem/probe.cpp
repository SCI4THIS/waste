// Capture real guest POSIX calls and LLVM's inline metadata representation.
// Functional assertions live in filesystem.wast; no substitute filesystem.
#include "llvm/Config/config.h"
#include "llvm/Support/FileSystem.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

namespace fs = llvm::sys::fs;

struct observed_status : fs::basic_file_status {
  using fs::basic_file_status::basic_file_status;
  int64_t access_seconds() const { return fs_st_atime; }
  int64_t modified_seconds() const { return fs_st_mtime; }
  uint32_t access_nanoseconds() const { return fs_st_atime_nsec; }
  uint32_t modified_nanoseconds() const { return fs_st_mtime_nsec; }
};

struct stat_record {
  uint64_t before;
  struct stat value;
  uint64_t after;
  int32_t result, error;
};
static_assert(sizeof(stat_record) == 152);

struct report {
  uint32_t metadata[16];
  uint64_t llvm_values[8];
  int32_t operations[8];
  int64_t clock_bounds[4];
  stat_record records[6];
};
static_assert(sizeof(report) == 1104);

static void prepare(stat_record &record) {
  memset(&record, 0xa5, sizeof(record));
  record.before = UINT64_C(0x0123456789abcdef);
  record.after = UINT64_C(0xfedcba9876543210);
  errno = 17;
}

extern "C" int main(int argc, char **argv, char **environment) {
  (void)argc; (void)argv; (void)environment;
  uint32_t host_platform = 0, unsupported = 0;
#ifdef __linux__
  host_platform |= 1;
#endif
#ifdef __EMSCRIPTEN__
  host_platform |= 2;
#endif
#ifdef _WIN32
  host_platform |= 4;
#endif
#ifdef __unix__
  host_platform |= 8;
#endif
#if defined(HAVE_REGISTER_FRAME) && HAVE_REGISTER_FRAME
  unsupported |= 1;
#endif
#if defined(HAVE_DEREGISTER_FRAME) && HAVE_DEREGISTER_FRAME
  unsupported |= 2;
#endif
#if defined(HAVE_UNW_ADD_DYNAMIC_FDE) && HAVE_UNW_ADD_DYNAMIC_FDE
  unsupported |= 4;
#endif
#if defined(HAVE_PTHREAD_RWLOCK_INIT) && HAVE_PTHREAD_RWLOCK_INIT
  unsupported |= 8;
#endif
  volatile int64_t large_size = INT64_C(0x100000002);
  observed_status status(fs::file_type::regular_file, fs::perms(0600),
                         INT64_C(0x100000003), 123456789,
                         INT64_C(0x100000004), 987654321, 123, 456, large_size);
  fs::file_status full(fs::file_type::regular_file, fs::perms(0600),
                       UINT64_C(0x100000005), 1, UINT64_C(0x100000006),
                       0, 0, 0, 0, 123, 456, large_size);
  report output = {
    {LLVM_ON_UNIX, LLVM_WASTE_GUEST, host_platform, unsupported,
     HAVE_STRUCT_STAT_ST_MTIME_NSEC, sizeof(struct stat), alignof(struct stat),
     sizeof(off_t), sizeof(ino_t), sizeof(dev_t), sizeof(fs::file_status_size_type),
     sizeof(fs::file_status_device_type), sizeof(fs::file_status_inode_type),
     status.getUser(), status.getGroup(), status.permissions()},
    {status.getSize(), full.getSize(), uint64_t(status.access_seconds()),
     uint64_t(status.modified_seconds()), status.access_nanoseconds(),
     status.modified_nanoseconds(), fs::file_status().getSize(),
     uint64_t(status.type() == fs::file_type::regular_file)},
    {}, {}, {}
  };
  const char *path = "/tmp/llvm-metadata-file";
  struct timespec before = {}, after = {};
  output.operations[3] = clock_gettime(CLOCK_REALTIME, &before);
  int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
  output.operations[0] = fd >= 0;
  output.operations[1] = write(fd, "metadata\n", 9);
  output.operations[2] = chmod(path, 0600);
  int directory = open("/tmp", O_RDONLY | O_DIRECTORY);
  output.operations[4] = directory >= 0;

  for (auto &record : output.records) prepare(record);
#define CAPTURE(index, call) do { \
    errno = 17; \
    output.records[index].result = (call); \
    output.records[index].error = errno; \
  } while (0)
  CAPTURE(0, stat(path, &output.records[0].value));
  CAPTURE(1, lstat(path, &output.records[1].value));
  CAPTURE(2, fstat(fd, &output.records[2].value));
  CAPTURE(3, fstatat(directory, "llvm-metadata-file", &output.records[3].value, 0));
  CAPTURE(4, stat("/tmp/llvm-metadata-absent", &output.records[4].value));
  CAPTURE(5, fstat(-1, &output.records[5].value));
#undef CAPTURE
  output.operations[5] = close(fd);
  output.operations[6] = close(directory);
  output.operations[7] = clock_gettime(CLOCK_REALTIME, &after);
  output.clock_bounds[0] = before.tv_sec;
  output.clock_bounds[1] = before.tv_nsec;
  output.clock_bounds[2] = after.tv_sec;
  output.clock_bounds[3] = after.tv_nsec;
  (void)unlink(path);
  return write(1, &output, sizeof(output)) != (ssize_t)sizeof(output);
}
