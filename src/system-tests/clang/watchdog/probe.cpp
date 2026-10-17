// Public alarm and actual LLVM Watchdog observations. WAST owns expectations.
#include "llvm/Support/Watchdog.h"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <type_traits>
#include <unistd.h>

static_assert(std::is_same_v<decltype(&alarm), unsigned (*)(unsigned)>);
static uint64_t output[128];
static unsigned count;
static volatile sig_atomic_t caught, last_signal;
static void record(uint64_t value) { output[count++] = value; }
static void handler(int sig) { caught++; last_signal = sig; }
static uint64_t now() {
  struct timespec value{};
  clock_gettime(CLOCK_MONOTONIC, &value);
  return uint64_t(value.tv_sec) * 1000000000u + value.tv_nsec;
}
#ifdef WASTE_WATCHDOG_CONTROLLED
static void sleep_ns(long long ns) {
  struct timespec duration{ns / 1000000000, long(ns % 1000000000)};
  record(nanosleep(&duration, nullptr));
}
static void mark(const char *text) { write(2, text, strlen(text)); }
#endif

extern "C" int main(int argc, char **argv, char **environment) {
  if (argc > 1 && !strcmp(argv[1], "exec")) {
    record(alarm(0));
    struct sigaction action{};
    record(sigaction(SIGALRM, nullptr, &action));
    record(action.sa_handler == SIG_DFL);
    return write(1, output, count * sizeof(*output)) != ssize_t(count * sizeof(*output));
  }
  struct sigaction action{};
  action.sa_handler = handler;
  sigemptyset(&action.sa_mask);
  record(sigaction(SIGALRM, &action, nullptr));
  errno = 73;
  record(alarm(0));
  record(alarm(3));
  record(alarm(2));
  record(alarm(0));
  record(alarm(0));
  record(alarm(0xffffffffu));
  record(alarm(0));
  record(errno);
  {
    llvm::sys::Watchdog watchdog(2);
    record(alarm(4));
  }
  record(alarm(0));
  alarm(3);
  { llvm::sys::Watchdog watchdog(0); }
  record(alarm(0));

#ifdef WASTE_WATCHDOG_CONTROLLED
  uint64_t start = now();
  record(alarm(3));
  mark("__WATCHDOG_WAIT_1__\n");
  sleep_ns(500000000);
  record(now() - start);
  record(alarm(2));
  mark("__WATCHDOG_WAIT_2__\n");
  sleep_ns(500000000);
  record(now() - start);
  record(alarm(0));
  record(caught);
  sigset_t mask{}, pending{};
  sigemptyset(&mask);
  sigaddset(&mask, SIGALRM);
  record(sigprocmask(SIG_BLOCK, &mask, nullptr));
  record(alarm(1));
  mark("__WATCHDOG_WAIT_3__\n");
  sleep_ns(2000000000);
  record(now() - start);
  record(caught);
  record(sigpending(&pending));
  record(sigismember(&pending, SIGALRM));
  record(alarm(0));
  record(sigpending(&pending));
  record(sigismember(&pending, SIGALRM));
  record(sigprocmask(SIG_UNBLOCK, &mask, nullptr));
  record(caught);
  record(last_signal);
  int pipefd[2];
  record(pipe(pipefd));
  record(alarm(1));
  mark("__WATCHDOG_WAIT_4__\n");
  char byte;
  record(read(pipefd[0], &byte, 1));
  record(errno);
  record(now() - start);
  record(caught);
  record(last_signal);
  record(alarm(0));
  close(pipefd[0]); close(pipefd[1]);
  {
    llvm::sys::Watchdog watchdog(2);
    mark("__WATCHDOG_WAIT_5__\n");
    sleep_ns(1000000000);
    record(alarm(2));
  }
  record(alarm(0));
  record(caught);
#else
  int pipefd[2];
  record(pipe(pipefd));
  uint64_t start = now();
  record(alarm(1));
  char byte;
  record(read(pipefd[0], &byte, 1));
  record(errno);
  record(caught);
  record(last_signal);
  record(now() - start);
  record(alarm(0));
  close(pipefd[0]); close(pipefd[1]);

  start = now();
  record(alarm(1));
  while (caught == 1) {} // Caught alarm must interrupt CPU work without an import.
  record(caught);
  record(last_signal);
  record(now() - start);
  record(alarm(0));

  record(alarm(0xffffffffu));
  record(pipe(pipefd));
  pid_t child = fork();
  if (!child) {
    uint64_t remaining = alarm(0);
    close(pipefd[0]);
    write(pipefd[1], &remaining, sizeof(remaining));
    _exit(0);
  }
  close(pipefd[1]);
  uint64_t remaining = ~uint64_t(0);
  record(read(pipefd[0], &remaining, sizeof(remaining)));
  record(remaining);
  close(pipefd[0]);
  int status = -1;
  record(waitpid(child, &status, 0) == child);
  record(status);
  record(alarm(0));

  record(pipe(pipefd));
  child = fork();
  if (!child) {
    close(pipefd[0]);
    dup2(pipefd[1], 1);
    close(pipefd[1]);
    alarm(0xffffffffu);
    char *args[] = {argv[0], const_cast<char *>("exec"), nullptr};
    execve(argv[0], args, environment);
    _exit(127);
  }
  close(pipefd[1]);
  uint64_t exec_values[3] = {~uint64_t(0), ~uint64_t(0), ~uint64_t(0)};
  record(read(pipefd[0], exec_values, sizeof(exec_values)));
  for (uint64_t value : exec_values) record(value);
  close(pipefd[0]);
  record(waitpid(child, &status, 0) == child);
  record(status);
  record(alarm(0));

  start = now();
  child = fork();
  if (!child) {
    signal(SIGALRM, SIG_DFL);
    llvm::sys::Watchdog watchdog(1);
    volatile unsigned spin = 0;
    for (;;) { ++spin; } // Default termination must stop CPU-bound work.
  }
  record(waitpid(child, &status, 0) == child);
  record(status);
  record(now() - start);

  child = fork();
  if (!child) { struct timespec wait{2, 0}; nanosleep(&wait, nullptr); _exit(0); }
  record(alarm(1));
  record(waitpid(child, &status, 0));
  record(errno);
  record(caught);
  record(waitpid(child, &status, 0) == child);
  record(status);
  record(alarm(0));

  pid_t children[2];
  start = now();
  for (unsigned i = 0; i != 2; ++i) {
    children[i] = fork();
    if (!children[i]) {
      signal(SIGALRM, SIG_DFL);
      llvm::sys::Watchdog watchdog(i + 1);
      struct timespec wait{10, 0}; nanosleep(&wait, nullptr); _exit(99);
    }
  }
  // Wait for the later timer first. The earlier sibling still must terminate.
  for (int i = 1; i >= 0; --i) {
    record(waitpid(children[i], &status, 0) == children[i]);
    record(status);
  }
  record(now() - start);
  record(caught);
  record(alarm(0));
#endif
  return write(1, output, count * sizeof(*output)) != ssize_t(count * sizeof(*output));
}
