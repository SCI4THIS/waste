// Record public guest C++ account calls; WAST owns every expectation.
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <pwd.h>
#include <string>
#include <type_traits>
#include <unistd.h>

extern "C" void waste_passwd_set(char *, char *, unsigned int, unsigned int,
                                 char *, char *, char *);
static_assert(std::is_same_v<decltype(&getpwnam_r),
              int (*)(const char *, passwd *, char *, size_t, passwd **)>);
static_assert(std::is_same_v<decltype(&getpwuid_r),
              int (*)(uid_t, passwd *, char *, size_t, passwd **)>);
static_assert(std::is_same_v<decltype(&sysconf), long (*)(int)>);

struct report { int values[40]; char strings[5][128]; };

extern "C" int main(int, char **, char **) {
  report result = {};
  passwd first = {}, second = {}, *found = nullptr;
  char one[128], two[128], small[8];
  result.values[0] = sizeof(passwd);
  result.values[1] = offsetof(passwd, pw_uid);
  result.values[2] = offsetof(passwd, pw_dir);
  result.values[3] = sizeof(size_t);
  errno = 71;
  result.values[4] = sysconf(_SC_PAGE_SIZE);
  result.values[5] = errno;
  errno = 72;
  result.values[6] = sysconf(_SC_GETPW_R_SIZE_MAX);
  result.values[7] = errno;
  result.values[8] = sysconf(-123);
  result.values[9] = errno;
  errno = 73;
  result.values[10] = getpwnam_r("root", &first, one, sizeof(one), &found);
  result.values[11] = found == &first;
  result.values[12] = errno;
  if (found) {
    result.values[13] = 1;
    for (char *field : {first.pw_name, first.pw_passwd, first.pw_gecos,
                        first.pw_dir, first.pw_shell})
      result.values[13] &= field >= one && field < one + sizeof(one);
    passwd *shared = getpwuid(0);
    result.values[14] = shared && first.pw_name != shared->pw_name;
    result.values[15] = first.pw_uid;
    result.values[16] = first.pw_gid;
    strcpy(result.strings[0], first.pw_name);
    strcpy(result.strings[1], first.pw_dir);
    strcpy(result.strings[2], first.pw_shell);
  }
  result.values[17] = getpwuid_r(0, &second, two, sizeof(two), &found);
  result.values[18] = found == &second;
  result.values[19] = found && first.pw_name != second.pw_name;
  result.values[20] = first.pw_name && strcmp(first.pw_name, "root") == 0;
  memset(small, '!', sizeof(small));
  memset(&second, '!', sizeof(second));
  errno = 74;
  result.values[21] = getpwuid_r(0, &second, small, 0, &found);
  result.values[22] = found == nullptr;
  result.values[23] = errno;
  result.values[24] = 1;
  for (char byte : small) result.values[24] &= byte == '!';
  result.values[25] = 1;
  for (unsigned char byte : reinterpret_cast<unsigned char (&)[sizeof(second)]>(second))
    result.values[25] &= byte == '!';
  errno = 75;
  result.values[26] = getpwnam_r("missing", &second, two, sizeof(two), &found);
  result.values[27] = found == nullptr;
  result.values[28] = errno;
  waste_passwd_set(const_cast<char *>("compiler"), const_cast<char *>("x"), 1000, 100,
                   const_cast<char *>("Compiler"), const_cast<char *>("/home/compiler"),
                   const_cast<char *>("/usr/bin/bash"));
  result.values[29] = getpwuid_r(1000, &second, two, sizeof(two), &found);
  if (found) {
    result.values[30] = second.pw_uid;
    result.values[31] = second.pw_gid;
    std::string directory(second.pw_dir);
    result.values[32] = directory.size();
    strcpy(result.strings[3], directory.c_str());
  }
  // Changing shared account data leaves earlier caller-owned results valid.
  if (first.pw_dir) strcpy(result.strings[4], first.pw_dir);
  result.values[33] = getpwnam_r("root", &second, two, sizeof(two), &found);
  result.values[34] = found == nullptr;
  errno = 76;
  result.values[35] = getpwuid_r(1000, nullptr, two, sizeof(two), &found);
  result.values[36] = errno;
  result.values[37] = found == nullptr;
  result.values[38] = _SC_PAGE_SIZE == _SC_PAGESIZE;
  result.values[39] = _SC_GETPW_R_SIZE_MAX;
  write(STDOUT_FILENO, &result, sizeof(result));
  return 0;
}
