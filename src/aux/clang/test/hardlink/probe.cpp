// Capture ordinary public C++ calls, including fork/wait. WAST owns expectations.
#include <cerrno>
#include <fcntl.h>
#include <cstdio>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int values[64];
static unsigned count;
static void record(int result) { values[count++] = result; values[count++] = errno; }
extern "C" int main(int, char **, char **) {
  errno = 73;
  record(mkdir("/tmp/hardlink-cxx", 0700));                         // 0
  int fd = open("/tmp/hardlink-cxx/file", O_CREAT | O_RDWR, 0644);
  record(fd < 0 ? fd : 0);                                      // 2
  errno = 73;
  record(write(fd, "OLD!", 4));                                 // 4
  errno = 73;
  record(link("/tmp/hardlink-cxx/file", "/tmp/hardlink-cxx/alias")); // 6
  struct stat original{}, alias{};
  errno = 73;
  record(stat("/tmp/hardlink-cxx/file", &original));              // 8
  values[10] = original.st_nlink;
  values[11] = original.st_ino != 0;
  count = 12;
  errno = 73;
  record(stat("/tmp/hardlink-cxx/alias", &alias));                 // 12
  values[14] = alias.st_ino == original.st_ino;
  count = 15;
  errno = 73;
  record(chmod("/tmp/hardlink-cxx/alias", 0600));                 // 15
  errno = 73;
  record(fstat(fd, &alias));                                    // 17
  values[19] = alias.st_mode & 0777;
  count = 20;
  errno = 73;
  record(symlink("file", "/tmp/hardlink-cxx/soft"));             // 20
  errno = 73;
  record(linkat(AT_FDCWD, "/tmp/hardlink-cxx/soft", AT_FDCWD,
                "/tmp/hardlink-cxx/soft-copy", 0));             // 22
  errno = 73;
  record(lstat("/tmp/hardlink-cxx/soft-copy", &alias));           // 24
  values[26] = alias.st_nlink;
  count = 27;
  errno = 73;
  record(linkat(AT_FDCWD, "/tmp/hardlink-cxx/soft", AT_FDCWD,
                "/tmp/hardlink-cxx/third", AT_SYMLINK_FOLLOW)); // 27
  errno = 73;
  record(fstat(fd, &alias));                                    // 29
  values[31] = alias.st_nlink;
  count = 32;
  int pid = fork();
  if (pid == 0) {
    int child[6]{};
    child[0] = link("/tmp/hardlink-cxx/file", "/tmp/hardlink-cxx/child-alias");
    child[1] = fstat(fd, &alias);
    child[2] = alias.st_nlink;
    int other = open("/tmp/hardlink-cxx/child-new", O_CREAT | O_RDWR, 0600);
    fstat(other, &alias);
    child[3] = static_cast<int>(alias.st_ino);
    child[4] = static_cast<int>(lseek(fd, 0, SEEK_SET));
    child[5] = write(fd, "NEW!", 4);
    close(other);
    int output = open("/tmp/hardlink-child.bin", O_CREAT | O_WRONLY, 0600);
    write(output, child, sizeof(child));
    close(output);
    _exit(0);
  }
  int other = open("/tmp/hardlink-cxx/parent-new", O_CREAT | O_RDWR, 0600);
  fstat(other, &alias);
  values[32] = static_cast<int>(alias.st_ino);
  close(other);
  int status = -1;
  int waited = pid > 0 ? waitpid(pid, &status, 0) : -1;
  values[33] = waited == pid && pid > 0;
  values[34] = status;
  values[35] = fstat(fd, &alias);
  values[36] = alias.st_nlink;
  lseek(fd, 0, SEEK_SET);
  values[37] = read(fd, &values[38], 4);
  close(fd);
  values[63] = 39;
  write(1, values, sizeof(values));
  return 0;
}
