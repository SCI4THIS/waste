// Capture compiled public C++ calls; symlink.wast owns expected results.
#include <cerrno>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

static int values[64];
static unsigned count;
static void record(int result) { values[count++] = result; values[count++] = errno; }
extern "C" int main(int, char **, char **) {
  errno = 73;
  record(mkdir("/tmp/symlink-cxx", 0700));
  errno = 73;
  record(symlink("later", "/tmp/symlink-cxx/link"));
  int directory = open("/tmp/symlink-cxx", O_RDONLY | O_DIRECTORY);
  errno = 73;
  record(symlinkat("../symlink-cxx/link", directory, "second"));
  errno = 73;
  record(symlinkat("later", -999, "/tmp/symlink-cxx/absolute"));
  errno = 0;
  record(symlink("changed", "/tmp/symlink-cxx/link"));
  errno = 0;
  record(symlinkat("later", -999, "bad"));
  errno = 0;
  record(symlink(nullptr, "/tmp/symlink-cxx/null"));
  errno = 0;
  record(symlink("later", nullptr));
  errno = 0;
  record(symlink("", "/tmp/symlink-cxx/empty"));
  errno = 0;
  record(symlink("later", ""));
  // The coordinated namespace limit comes from Clang's private config adapter.
  char path[PATH_MAX + 1];
  path[0] = '/';
  for (unsigned i = 1; i < PATH_MAX; ++i) path[i] = 'x';
  path[PATH_MAX] = 0;
  errno = 0;
  record(symlink("later", path));
  errno = 73;
  record(close(directory));
  values[63] = static_cast<int>(count);
  write(1, values, sizeof(values));
  return 0;
}
