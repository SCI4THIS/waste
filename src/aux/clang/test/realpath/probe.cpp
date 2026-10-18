// Capture ordinary guest C++ calls. realpath.wast owns every expectation.
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <type_traits>
#include <unistd.h>

static_assert(std::is_same_v<decltype(&realpath),
              char *(*)(const char *, char *)>);
extern "C" int raw_realpath(const char *, char *, size_t)
    __attribute__((import_module("waste_kernel"), import_name("realpath_v1")));

struct report { int values[64]; char strings[4][128]; };

extern "C" int main(int, char **, char **) {
  report result = {};
  result.values[0] = sizeof(char *);
  result.values[1] = sizeof(size_t);
  result.values[2] = mkdir("/tmp/realpath-cxx", 0700);
  result.values[3] = mkdir("/tmp/realpath-cxx/sub", 0700);
  int file = open("/tmp/realpath-cxx/file", O_CREAT | O_RDWR, 0600);
  result.values[4] = file >= 0;
  result.values[5] = close(file);
  result.values[6] = symlink("sub", "/tmp/realpath-cxx/link");
  result.values[7] = chdir("/tmp/realpath-cxx");
  char buffer[PATH_MAX];
  memset(buffer, '!', sizeof(buffer));
  errno = 73;
  result.values[8] = realpath("link/..", buffer) == buffer;
  result.values[9] = errno;
  strcpy(result.strings[0], buffer);
  result.values[10] = strcmp(buffer, "/tmp/realpath-cxx");
  result.values[11] = buffer[strlen(buffer) + 1] == '!';
  errno = 74;
  char *first = realpath("file", nullptr);
  result.values[12] = first != nullptr;
  result.values[13] = errno;
  errno = 75;
  char *second = realpath("/", nullptr);
  result.values[14] = first && second && first != second;
  if (first) {
    strcpy(result.strings[1], first);
    result.values[15] = strcmp(first, "/tmp/realpath-cxx/file");
    result.values[16] = strcmp(first, "/tmp/realpath-cxx/file") == 0;
  }
  result.values[17] = errno;
  free(second);
  if (first) {
    char *grown = static_cast<char *>(realloc(first, PATH_MAX * 2));
    result.values[36] = grown && strcmp(grown, "/tmp/realpath-cxx/file") == 0;
    free(grown ? grown : first);
  }
  memset(buffer, '!', sizeof(buffer));
  result.values[18] = realpath("missing", buffer) == nullptr;
  result.values[19] = errno;
  result.values[20] = 1;
  for (char c : buffer) result.values[20] &= c == '!';
  result.values[21] = chmod("file", 0);
  errno = 76;
  result.values[22] = realpath("file", buffer) != nullptr;
  result.values[23] = errno;
  result.values[24] = strcmp(buffer, "/tmp/realpath-cxx/file");
  result.values[25] = realpath(nullptr, buffer) == nullptr;
  result.values[26] = errno;
  result.values[27] = realpath("", buffer) == nullptr;
  result.values[28] = errno;
  strcpy(buffer, "link/..");
  result.values[29] = realpath(buffer, buffer) == buffer;
  result.values[30] = strcmp(buffer, "/tmp/realpath-cxx");
  errno = 77;
  result.values[31] = raw_realpath("/", buffer, 2);
  result.values[32] = errno;
  result.values[33] = raw_realpath("/", buffer, 0);
  result.values[34] = raw_realpath("/", reinterpret_cast<char *>(-1), PATH_MAX);
  result.values[35] = raw_realpath(reinterpret_cast<const char *>(-1), buffer, PATH_MAX);
  strcpy(result.strings[2], buffer);

  // Check the runtime's bounded copy across a protected page, without guest
  // loads/stores touching the protected bytes. Page size uses the shared ABI.
  const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  char *mapping = static_cast<char *>(mmap(nullptr, page * 2,
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  result.values[37] = mapping != MAP_FAILED;
  if (mapping != MAP_FAILED) {
    char *edge = mapping + page - 1;
    *edge = '!';
    result.values[38] = mprotect(mapping + page, page, PROT_NONE);
    errno = 0;
    result.values[39] = realpath("/", edge) == nullptr;
    result.values[40] = errno;
    result.values[41] = *edge == '!';
    memset(buffer, '!', sizeof(buffer));
    result.values[42] = realpath(edge, buffer) == nullptr;
    result.values[43] = errno;
    result.values[44] = 1;
    for (char c : buffer) result.values[44] &= c == '!';
    result.values[45] = mprotect(mapping + page, page, PROT_READ | PROT_WRITE);
    result.values[46] = munmap(mapping, page * 2);
  }
  result.values[47] = getcwd(buffer, sizeof(buffer)) != nullptr;
  strcpy(result.strings[3], buffer);
  result.values[48] = chdir("/");
  result.values[49] = raw_realpath("/", buffer, static_cast<size_t>(-1));
  char overlong[PATH_MAX + 1];
  memset(overlong, 'x', PATH_MAX);
  overlong[0] = '/';
  overlong[PATH_MAX] = 0;
  result.values[50] = realpath(overlong, buffer) == nullptr;
  result.values[51] = errno;
  write(STDOUT_FILENO, &result, sizeof(result));
  return 0;
}
