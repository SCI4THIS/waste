// Ordinary guest processes observe public fcntl and unchanged LLVM methods.
// Every runtime expectation is in locks.wast.
#include "llvm/Support/FileSystem.h"
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <cstdio>
#include <time.h>
#include <type_traits>
#include <unistd.h>

static_assert(sizeof(flock) == WASTE_FLOCK_BYTES);
static_assert(offsetof(flock, l_type) == WASTE_FLOCK_TYPE_OFFSET);
static_assert(offsetof(flock, l_whence) == WASTE_FLOCK_WHENCE_OFFSET);
static_assert(offsetof(flock, l_start) == WASTE_FLOCK_START_OFFSET);
static_assert(offsetof(flock, l_len) == WASTE_FLOCK_LEN_OFFSET);
static_assert(offsetof(flock, l_pid) == WASTE_FLOCK_PID_OFFSET);
static_assert(std::is_same_v<decltype(&fcntl), int (*)(int, int, ...)>);

static int values[32][16];
static volatile sig_atomic_t caught;
static int signal_fd = -1;
static void handler(int) {
  ++caught;
  if (signal_fd >= 0) lseek(signal_fd, 200, SEEK_SET);
}
static flock range(int type, int start, int length, int whence = SEEK_SET) {
  return {static_cast<short>(type), static_cast<short>(whence), start, length, 77};
}
static int set(int fd, int type, int start, int length, int whence = SEEK_SET) {
  flock lock = range(type, start, length, whence);
  return fcntl(fd, F_SETLK, &lock);
}
static void query(int *out, int fd, int type, int start, int length) {
  flock lock = range(type, start, length);
  out[0] = fcntl(fd, F_GETLK, &lock);
  out[1] = lock.l_type; out[2] = lock.l_whence; out[3] = lock.l_start;
  out[4] = lock.l_len; out[5] = lock.l_pid;
}
static void pause_ms(int ms) {
  timespec delay = {0, ms * 1000000L};
  nanosleep(&delay, nullptr);
}
static void child_report(int row) {
  int fd = open("/tmp/locks-child", O_CREAT | O_TRUNC | O_WRONLY, 0600);
  write(fd, values[row], sizeof(values[row]));
  close(fd);
  _exit(0);
}
static void collect(int row, int pid) {
  int status = -1;
  values[30 + row / 16][row % 16] = waitpid(pid, &status, 0) == pid ? status : -999;
  int fd = open("/tmp/locks-child", O_RDONLY);
  read(fd, values[row], sizeof(values[row]));
  close(fd);
}
static void ready(int fd) { write(fd, "R", 1); }
static void await(int fd) { char byte; read(fd, &byte, 1); }
static void number(char *output, int value) {
  int index = 0;
  do { output[index++] = '0' + value % 10; value /= 10; } while (value);
  output[index] = 0;
  for (int i = 0; i < index / 2; ++i) {
    char c = output[i]; output[i] = output[index - i - 1]; output[index - i - 1] = c;
  }
}

extern "C" int main(int argc, char **argv, char **) {
  using llvm::sys::fs::LockKind;
  if (argc == 5) {
    int row = atoi(argv[1]), fd = atoi(argv[2]);
    ready(atoi(argv[3])); await(atoi(argv[4]));
    values[row][6] = fcntl(fd, F_GETFD);
    values[row][7] = errno;
    child_report(row);
  }
  int fd = open("/tmp/locks-file", O_CREAT | O_TRUNC | O_RDWR, 0600);
  values[0][0] = fd >= 0; values[0][1] = ftruncate(fd, 20);
  values[0][2] = getpid();
  errno = 91;
  flock original = range(F_WRLCK, 10, 10), lock = original;
  values[0][3] = fcntl(fd, F_SETLK, &lock);
  values[0][4] = errno; values[0][5] = memcmp(&lock, &original, sizeof(lock));
  int pid = fork();
  if (!pid) {
    query(values[1], fd, F_WRLCK, 12, 1);
    errno = 0; values[1][6] = set(fd, F_WRLCK, 12, 1); values[1][7] = errno;
    errno = 0; values[1][8] = set(fd, F_RDLCK, 12, 1); values[1][9] = errno;
    values[1][10] = close(fd);
    fd = open("/tmp/locks-file", O_RDWR);
    query(values[1] + 10, fd, F_WRLCK, 12, 1);
    child_report(1);
  }
  collect(1, pid);
  values[2][0] = set(fd, F_UNLCK, 14, 2);
  pid = fork();
  if (!pid) {
    query(values[3], fd, F_WRLCK, 13, 1);
    query(values[4], fd, F_WRLCK, 14, 1);
    query(values[5], fd, F_WRLCK, 16, 1);
    // Write three adjacent records; collect reads the complete file below.
    int out = open("/tmp/locks-child", O_CREAT | O_TRUNC | O_WRONLY, 0600);
    write(out, values[3], sizeof(values[3]) * 3); close(out); _exit(0);
  }
  int status = -1; values[2][1] = waitpid(pid, &status, 0) == pid ? status : -999;
  int out = open("/tmp/locks-child", O_RDONLY);
  values[2][2] = read(out, values[3], sizeof(values[3]) * 3); close(out);
  values[6][0] = set(fd, F_WRLCK, 10, 10);
  values[6][1] = set(fd, F_RDLCK, 12, 2);
  pid = fork();
  if (!pid) {
    query(values[7], fd, F_WRLCK, 12, 1);
    query(values[7] + 6, fd, F_RDLCK, 12, 1);
    child_report(7);
  }
  collect(7, pid);
  values[8][0] = set(fd, F_UNLCK, 0, 0);
  values[8][1] = llvm::sys::fs::lockFile(fd, LockKind::Shared).value();
  pid = fork();
  if (!pid) {
    values[9][0] = llvm::sys::fs::lockFile(fd, LockKind::Shared).value();
    errno = 0; values[9][1] = set(fd, F_WRLCK, 0, 0); values[9][2] = errno;
    values[9][3] = llvm::sys::fs::unlockFile(fd).value();
    query(values[9] + 4, fd, F_WRLCK, 1000, 1);
    child_report(9);
  }
  collect(9, pid);
  values[10][0] = llvm::sys::fs::unlockFile(fd).value();
  values[10][1] = lseek(fd, 5, SEEK_SET);
  values[10][2] = set(fd, F_WRLCK, 3, -2, SEEK_CUR);
  values[10][3] = set(fd, F_WRLCK, -4, 0, SEEK_END);
  values[10][4] = lseek(fd, 0, SEEK_CUR);
  pid = fork();
  if (!pid) {
    query(values[11], fd, F_WRLCK, 6, 1);
    query(values[11] + 6, fd, F_WRLCK, 1000, 1);
    child_report(11);
  }
  collect(11, pid);
  int alias = dup(fd);
  values[12][0] = alias >= 0; values[12][1] = close(alias);
  pid = fork();
  if (!pid) { query(values[13], fd, F_WRLCK, 6, 1); child_report(13); }
  collect(13, pid);

  // Blocking, interruption and restart use the same readiness scheduler as sleep.
  for (int row = 14; row <= 16; ++row) {
    int channel[2], acquired[2], release[2];
    pipe(channel); pipe(acquired); pipe(release);
    values[row][10] = set(fd, F_WRLCK, 0, 0);
    pid = fork();
    if (!pid) {
      struct sigaction action = {};
      action.sa_handler = handler; action.sa_flags = row == 16 ? SA_RESTART : 0;
      sigemptyset(&action.sa_mask);
      values[row][0] = sigaction(SIGUSR1, &action, nullptr);
      close(channel[0]); ready(channel[1]); close(channel[1]);
      errno = 92;
      if (row == 14) values[row][1] = llvm::sys::fs::lockFile(fd, LockKind::Exclusive).value();
      else {
        flock request = range(F_WRLCK, 0, 0);
        if (row == 16) {
          signal_fd = fd; lseek(fd, 5, SEEK_SET);
          request = range(F_WRLCK, -5, 1, SEEK_CUR);
        }
        values[row][1] = fcntl(fd, F_SETLKW, &request);
      }
      values[row][2] = errno; values[row][3] = caught;
      if (row == 16) {
        values[row][13] = lseek(fd, 0, SEEK_CUR);
        ready(acquired[1]); await(release[0]);
      }
      child_report(row);
    }
    close(channel[1]); await(channel[0]); close(channel[0]); pause_ms(10);
    int held = values[row][10];
    if (row != 14) kill(pid, SIGUSR1);
    if (row == 16) pause_ms(10);
    if (row != 15) set(fd, F_UNLCK, 0, 0);
    int observed[6] = {};
    if (row == 16) {
      await(acquired[0]); query(observed, fd, F_WRLCK, 0, 1); ready(release[1]);
    }
    collect(row, pid); values[row][10] = held;
    if (row == 16) { memcpy(values[row] + 4, observed, sizeof(observed)); values[row][12] = pid; }
    close(acquired[0]); close(acquired[1]); close(release[0]); close(release[1]);
    values[row][11] = set(fd, F_UNLCK, 0, 0);
  }

  // Exec preserves process ownership; closing a CLOEXEC alias releases locks.
  for (int row = 17; row <= 18; ++row) {
    int notify[2], release[2]; pipe(notify); pipe(release);
    pid = fork();
    if (!pid) {
      close(notify[0]); close(release[1]);
      set(fd, F_WRLCK, 100, 1);
      if (row == 18) { int other = dup(fd); (void)other; fcntl(fd, F_SETFD, FD_CLOEXEC); }
      char a[12], b[12], c[12], d[12];
      number(a, row); number(b, fd); number(c, notify[1]); number(d, release[0]);
      char *args[] = {argv[0], a, b, c, d, nullptr};
      char *environment[] = {nullptr};
      execve(argv[0], args, environment); _exit(99);
    }
    close(notify[1]); close(release[0]); await(notify[0]); close(notify[0]);
    int observed[6]; query(observed, fd, F_WRLCK, 100, 1);
    ready(release[1]); close(release[1]); collect(row, pid);
    memcpy(values[row], observed, sizeof(observed)); values[row][8] = pid;
  }
  // Two process wait cycle: parent gets EDEADLK, releases A, child resumes.
  int second = open("/tmp/locks-second", O_CREAT | O_RDWR, 0600), channel[2];
  pipe(channel); set(fd, F_WRLCK, 0, 1);
  pid = fork();
  if (!pid) {
    values[19][0] = set(second, F_WRLCK, 0, 1);
    close(channel[0]); ready(channel[1]); close(channel[1]);
    flock request = range(F_WRLCK, 0, 1);
    values[19][1] = fcntl(fd, F_SETLKW, &request); child_report(19);
  }
  close(channel[1]); await(channel[0]); close(channel[0]); pause_ms(10);
  lock = range(F_WRLCK, 0, 1); errno = 0;
  values[20][0] = fcntl(second, F_SETLKW, &lock); values[20][1] = errno;
  set(fd, F_UNLCK, 0, 0); collect(19, pid);
  query(values[20] + 2, second, F_WRLCK, 0, 1);
  close(second);
  // Locks follow inode aliases, including an unlinked descriptor; path reuse
  // creates a different object. Closing an independent alias releases our locks.
  values[22][0] = set(fd, F_WRLCK, 40, 4);
  values[22][1] = link("/tmp/locks-file", "/tmp/locks-alias");
  values[22][2] = symlink("/tmp/locks-alias", "/tmp/locks-symlink");
  values[22][3] = rename("/tmp/locks-file", "/tmp/locks-renamed");
  values[22][4] = unlink("/tmp/locks-renamed");
  int replacement = open("/tmp/locks-file", O_CREAT | O_RDWR, 0600);
  values[22][5] = replacement >= 0; close(replacement);
  pid = fork();
  if (!pid) {
    int linked = open("/tmp/locks-alias", O_RDWR);
    query(values[23], linked, F_WRLCK, 40, 1); close(linked);
    linked = open("/tmp/locks-symlink", O_RDWR);
    query(values[24], linked, F_WRLCK, 40, 1); close(linked);
    linked = open("/tmp/locks-file", O_RDWR);
    query(values[25], linked, F_WRLCK, 40, 1); close(linked);
    int output = open("/tmp/locks-child", O_CREAT | O_TRUNC | O_WRONLY, 0600);
    write(output, values[23], sizeof(values[23]) * 3); close(output); _exit(0);
  }
  status = -1; values[22][6] = waitpid(pid, &status, 0) == pid ? status : -999;
  out = open("/tmp/locks-child", O_RDONLY);
  values[22][7] = read(out, values[23], sizeof(values[23]) * 3); close(out);
  alias = open("/tmp/locks-alias", O_RDWR);
  values[22][8] = close(alias);
  pid = fork();
  if (!pid) { query(values[26], fd, F_WRLCK, 40, 1); child_report(26); }
  collect(26, pid);
  // Full output validation must precede stores, even across protection edges.
  size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  char *mapping = static_cast<char *>(mmap(nullptr, page * 2,
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  values[27][0] = mapping != MAP_FAILED;
  if (mapping != MAP_FAILED) {
    char *edge = mapping + page - 8;
    lock = range(F_WRLCK, 0, 1);
    memcpy(edge, &lock, sizeof(lock));
    values[27][1] = mprotect(mapping + page, page, PROT_READ);
    errno = 0; values[27][2] = fcntl(fd, F_GETLK, reinterpret_cast<flock *>(edge));
    values[27][3] = errno;
    values[27][4] = memcmp(edge, &lock, sizeof(lock));
    values[27][5] = mprotect(mapping + page, page, PROT_NONE);
    errno = 0; values[27][6] = fcntl(fd, F_SETLK, reinterpret_cast<flock *>(edge));
    values[27][7] = errno;
    values[27][8] = mprotect(mapping + page, page, PROT_READ | PROT_WRITE);
    values[27][9] = memcmp(edge, &lock, sizeof(lock));
    values[27][10] = munmap(mapping, page * 2);
  }
  // Death releases locks before reaping. Synchronize acquisition using a pipe.
  pipe(channel); pid = fork();
  if (!pid) {
    close(channel[0]); set(fd, F_WRLCK, 200, 1); ready(channel[1]); close(channel[1]);
    for (;;) pause_ms(100);
  }
  close(channel[1]); await(channel[0]); close(channel[0]);
  query(values[28], fd, F_WRLCK, 200, 1);
  values[28][6] = kill(pid, SIGKILL);
  status = -1; values[28][7] = waitpid(pid, &status, 0) == pid ? status : -999;
  query(values[29], fd, F_WRLCK, 200, 1);
  close(fd);
  values[21][0] = llvm::sys::fs::lockFile(-1, LockKind::Exclusive).value();
  values[21][1] = llvm::sys::fs::unlockFile(-1).value();
  write(STDOUT_FILENO, values, sizeof(values));
  return 0;
}
