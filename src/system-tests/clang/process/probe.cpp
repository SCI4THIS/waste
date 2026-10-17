// Observe the real LLVM Process object and public guest APIs; WAST owns checks.
#include "llvm/Config/config.h"
#include "llvm/Support/Process.h"
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <type_traits>
#include <unistd.h>

static_assert(sizeof(timeval) == 16 && sizeof(rusage) == 88);
static_assert(offsetof(rusage, ru_utime) == 0 && offsetof(rusage, ru_stime) == 16);
static_assert(offsetof(rusage, ru_maxrss) == 32);
static_assert(std::is_same_v<decltype(&getrusage), int (*)(int, rusage *)>);
static_assert(std::is_same_v<decltype(&getrlimit), int (*)(int, rlimit *)>);
static_assert(std::is_same_v<decltype(&_Exit), void (*)(int)>);
// WAST floors the lower clock sample to libc++'s system_clock resolution.
static_assert(std::is_same_v<std::chrono::system_clock::duration,
                             std::chrono::microseconds>);

extern "C" void legacy_exit(int)
  __attribute__((import_module("env"), import_name("_Exit")));
static int64_t output[64];
static unsigned count;
static int signal_fd;
static void record(int64_t value) { output[count++] = value; }
static void caught(int) { write(signal_fd, "S", 1); }
static int64_t nanos(const timespec &time) {
  return time.tv_sec * INT64_C(1000000000) + time.tv_nsec;
}

extern "C" int main(int, char **, char **) {
  record(LLVM_WASTE_GUEST);
  record(LLVM_ENABLE_THREADS);
#ifdef HAVE_GETRUSAGE
  record(1);
#else
  record(0);
#endif
  record(llvm::sys::Process::getProcessId() == getpid());
  timespec before = {}, after = {};
  record(clock_gettime(CLOCK_REALTIME, &before));
  llvm::sys::TimePoint<> elapsed;
  std::chrono::nanoseconds user, system;
  errno = 73;
  llvm::sys::Process::GetTimeUsage(elapsed, user, system);
  record(errno);
  record(clock_gettime(CLOCK_REALTIME, &after));
  record(nanos(before)); record(elapsed.time_since_epoch().count()); record(nanos(after));
  record(user.count()); record(system.count());
  struct rlimit limit = {23, 47}, observed = {};
  record(setrlimit(RLIMIT_CORE, &limit)); record(getrlimit(RLIMIT_CORE, &observed));
  record(observed.rlim_cur); record(observed.rlim_max);
  record(sizeof(rusage)); record(offsetof(rusage, ru_stime));
  const int codes[] = {0x142, -1, 0x280, 0x100};
  for (unsigned mode = 0; mode < 4; ++mode) {
    int channel[2];
    record(pipe(channel));
    int pid = fork();
    if (!pid) {
      close(channel[0]); signal_fd = channel[1];
      struct sigaction action = {};
      action.sa_handler = caught; sigemptyset(&action.sa_mask);
      sigaction(SIGUSR1, &action, nullptr);
      sigset_t mask; sigemptyset(&mask); sigaddset(&mask, SIGUSR1);
      sigprocmask(SIG_BLOCK, &mask, nullptr); raise(SIGUSR1);
      write(channel[1], "B", 1);
      if (mode < 2) _Exit(codes[mode]);
      if (mode == 2) _exit(codes[mode]);
      legacy_exit(codes[mode]);
      write(channel[1], "A", 1);
      _exit(99);
    }
    close(channel[1]);
    int status = -1;
    record(waitpid(pid, &status, 0) == pid && pid > 0);
    record(status); record(WIFEXITED(status)); record(WEXITSTATUS(status));
    char bytes[3] = {};
    record(read(channel[0], bytes, sizeof(bytes))); record(bytes[0]);
    record(read(channel[0], bytes, sizeof(bytes)));
    record(close(channel[0]));
  }
  record(getpid() > 0);
  return write(1, output, count * sizeof(output[0])) !=
         static_cast<ssize_t>(count * sizeof(output[0]));
}
