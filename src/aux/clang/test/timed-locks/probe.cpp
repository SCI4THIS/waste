// Capture public usleep and unchanged LLVM timed lock observations for WAST.
#include "llvm/Config/config.h"
#include "llvm/Support/FileSystem.h"
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>

using namespace std::chrono;
using llvm::sys::fs::LockKind;
static_assert(std::is_same_v<useconds_t, unsigned int>);
static_assert(std::is_same_v<decltype(&usleep), int (*)(useconds_t)>);
static int64_t output[64];
static unsigned count;
static volatile sig_atomic_t caught;
static void handler(int) { ++caught; }
static void record(int64_t value) { output[count++] = value; }

#ifndef USLEEP_CONTROLLED
static void child(int fd, int notify, int mode) {
  int64_t values[6] = {};
  struct sigaction action = {};
  action.sa_handler = handler;
  sigemptyset(&action.sa_mask);
  values[5] = sigaction(SIGUSR1, &action, nullptr);
  auto before = steady_clock::now();
  write(notify, "R", 1);
  close(notify);
  errno = 0;
  auto error = llvm::sys::fs::tryLockFile(fd,
      mode < 2 ? 0ms : mode == 2 ? 200ms : 2000ms,
      mode == 0 ? LockKind::Shared : LockKind::Exclusive);
  values[0] = error.value();
  values[1] = &error.category() == &std::generic_category();
  values[2] = errno;
  values[3] = caught;
  values[4] = duration_cast<nanoseconds>(steady_clock::now() - before).count();
  int out = open("/tmp/timed-locks-child", O_CREAT | O_TRUNC | O_WRONLY, 0600);
  write(out, values, sizeof(values));
  close(out);
  _exit(0);
}
#endif

extern "C" int main(int, char **, char **) {
  record(LLVM_WASTE_GUEST);
  record(LLVM_ENABLE_THREADS);
  record(sizeof(useconds_t));
  errno = 73; record(usleep(0)); record(errno);
  record(usleep(1000000)); record(errno);
  record(usleep(UINT32_MAX)); record(errno);
#ifdef USLEEP_CONTROLLED
  struct sigaction action = {};
  action.sa_handler = handler;
  sigemptyset(&action.sa_mask);
  record(sigaction(SIGUSR1, &action, nullptr));
  const useconds_t delays[] = {1, 1000, 999999, 500000};
  char marker[] = "__USLEEP_WAIT_1__\n";
  for (unsigned index = 0; index < 4; ++index) {
    marker[14] = '1' + index;
    write(2, marker, sizeof(marker) - 1);
    auto before = steady_clock::now();
    errno = 73;
    record(usleep(delays[index])); record(errno);
    record(duration_cast<nanoseconds>(steady_clock::now() - before).count());
  }
  record(caught);
#else
  auto error = llvm::sys::fs::tryLockFile(-1, 5ms, LockKind::Exclusive);
  record(error.value()); record(&error.category() == &std::generic_category());
  int fd = open("/tmp/timed-locks-file", O_CREAT | O_TRUNC | O_RDWR, 0600);
  record(fd >= 0);
  record(llvm::sys::fs::tryLockFile(fd, 0ms, LockKind::Exclusive).value());
  record(llvm::sys::fs::unlockFile(fd).value());
  for (int mode = 0; mode < 4; ++mode) {
    record(llvm::sys::fs::lockFile(fd, mode == 0 ? LockKind::Shared : LockKind::Exclusive).value());
    int notify[2]; pipe(notify);
    int pid = fork();
    if (!pid) { close(notify[0]); child(fd, notify[1], mode); }
    close(notify[1]);
    char byte; read(notify[0], &byte, 1); close(notify[0]);
    if (mode == 2) { usleep(3000); kill(pid, SIGUSR1); }
    if (mode == 3) { usleep(20000); llvm::sys::fs::unlockFile(fd); }
    int status = -1;
    record(waitpid(pid, &status, 0) == pid ? status : -999);
    int out = open("/tmp/timed-locks-child", O_RDONLY);
    int64_t values[6] = {};
    record(read(out, values, sizeof(values))); close(out);
    for (auto value : values) record(value);
    llvm::sys::fs::unlockFile(fd);
  }
  record(close(fd));
#endif
  return write(1, output, count * sizeof(output[0])) !=
         static_cast<ssize_t>(count * sizeof(output[0]));
}
