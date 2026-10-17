// Actual archive-linked LLVM calls. Runtime expectations live only in WAST.
#include "llvm/Support/Program.h"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

using llvm::StringRef;
using llvm::sys::ProcessInfo;
using llvm::sys::Wait;
static uint64_t Output[512];
static unsigned Count;
static volatile sig_atomic_t Caught;
static void handler(int) { ++Caught; }
static void add(int64_t value) { Output[Count++] = value; }
static void text(const std::string &value) {
  uint64_t words[8] = {};
  add(value.size());
  memcpy(words, value.data(), value.size() < sizeof(words) ? value.size() : sizeof(words));
  for (auto word : words) add(word);
}
static uint64_t now() {
  timespec value{};
  clock_gettime(CLOCK_MONOTONIC, &value);
  return uint64_t(value.tv_sec) * 1000000000 + value.tv_nsec;
}
static ProcessInfo process(pid_t pid) {
  ProcessInfo value; value.Pid = pid; value.Process = pid; return value;
}
static void result(ProcessInfo value, pid_t expected, const std::string &error) {
  add(expected > 0 ? value.Pid == expected : value.Pid);
  add(value.ReturnCode); text(error);
}
static void live(pid_t pid) { errno = 0; add(kill(pid, 0)); add(errno); }
static pid_t held(int (&gate)[2]) {
  add(pipe(gate));
  pid_t pid = fork();
  if (!pid) { close(gate[1]); char byte; read(gate[0], &byte, 1); _Exit(39); }
  add(pid > 0); close(gate[0]); return pid;
}
static int finish() {
  return write(1, Output, Count * sizeof(*Output)) != ssize_t(Count * sizeof(*Output));
}
#ifdef WASTE_FINITE_WAIT_CONTROLLED
static void mark(const char *value) { write(2, value, strlen(value)); }
static int saturation() {
  int gate[2]; pid_t pid = held(gate);
  std::string error; errno = 73; uint64_t start = now();
  mark("__FINITE_SATURATION_WAIT__\n");
  auto value = Wait(process(pid), UINT32_MAX, &error, nullptr, true);
  int saved_errno = errno;
  result(value, 0, error); add(saved_errno); add(now() - start); live(pid);
  add(write(gate[1], "R", 1)); add(close(gate[1]));
  value = Wait(process(pid), std::nullopt, &error);
  result(value, pid, error);
  return finish();
}
#endif

extern "C" int main(int argc, char **argv, char **) {
  if (argc > 1 && !strcmp(argv[1], "child-short")) {
#ifdef WASTE_FINITE_WAIT_CONTROLLED
    timespec duration{1, 0}; nanosleep(&duration, nullptr);
#else
    usleep(20000);
#endif
    return 37;
  }
  if (argc > 1 && (!strcmp(argv[1], "child-hold") || !strcmp(argv[1], "child-spin"))) {
    pid_t pid = getpid();
    int fd = open("/tmp/finite-wait-child-pid", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) { write(fd, &pid, sizeof(pid)); close(fd); }
    if (!strcmp(argv[1], "child-spin")) {
      volatile unsigned counter = 0;
      for (;;) { ++counter; }
    }
    timespec duration{5, 0}; nanosleep(&duration, nullptr);
    fd = open("/tmp/finite-wait-survived", O_WRONLY | O_CREAT, 0600);
    if (fd >= 0) close(fd);
    return 51;
  }
#ifdef WASTE_FINITE_WAIT_CONTROLLED
  if (argc > 1 && !strcmp(argv[1], "saturation")) return saturation();
#endif
  StringRef self(argv[0]);
  struct sigaction action{}, observed{};
  action.sa_handler = handler; sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  add(sigaction(SIGALRM, &action, nullptr));
  int gate[2]; pid_t blocked = held(gate);
  std::string error;
  uint64_t start = now(); errno = 73;

#ifdef WASTE_FINITE_WAIT_CONTROLLED
  mark("__FINITE_WAIT_1__\n");
  auto value = Wait(process(blocked), 1, &error, nullptr, true);
  int saved_errno = errno;
  result(value, 0, error); add(saved_errno); add(now() - start); live(blocked);

  sigset_t mask{}, pending{};
  sigemptyset(&mask); sigaddset(&mask, SIGALRM);
  add(sigprocmask(SIG_BLOCK, &mask, nullptr)); add(alarm(1));
  start = now(); errno = 73;
  mark("__FINITE_WAIT_2__\n");
  value = Wait(process(blocked), 2, &error, nullptr, true);
  saved_errno = errno;
  result(value, 0, error); add(saved_errno); add(now() - start); add(Caught);
  add(sigpending(&pending)); add(sigismember(&pending, SIGALRM)); add(alarm(0));
  add(sigprocmask(SIG_UNBLOCK, &mask, nullptr)); add(Caught);

  for (unsigned restart = 0; restart < 2; ++restart) {
    Caught = 0; action.sa_flags = restart ? SA_RESTART : 0;
    add(sigaction(SIGALRM, &action, nullptr)); add(alarm(1));
    start = now(); errno = 73;
    mark(restart ? "__FINITE_WAIT_4__\n" : "__FINITE_WAIT_3__\n");
    value = Wait(process(blocked), 3, &error, nullptr, true);
    saved_errno = errno;
    result(value, 0, error); add(saved_errno); add(now() - start); add(Caught);
  }
  add(alarm(4)); start = now();
  mark("__FINITE_WAIT_5__\n");
  value = Wait(process(blocked), 1, &error, nullptr, true);
  result(value, 0, error); add(now() - start); add(alarm(0));
  add(sigaction(SIGALRM, nullptr, &observed));
  add(observed.sa_handler == handler); add(observed.sa_flags);

  pid_t sibling = fork(); if (!sibling) _Exit(29);
  start = now();
  mark("__FINITE_WAIT_6__\n");
  value = Wait(process(blocked), 1, &error);
  result(value, blocked, error); add(now() - start); live(blocked);
  int status = -1;
  add(waitpid(sibling, &status, 0) == sibling); add(status); add(close(gate[1]));
  error.clear(); bool failed = true;
  StringRef short_args[] = {self, "child-short"};
  start = now();
  mark("__FINITE_WAIT_7__\n");
  add(llvm::sys::ExecuteAndWait(self, short_args, std::nullopt, {}, 2, 0, &error, &failed));
  add(failed); text(error); add(now() - start);
  error.clear(); failed = true;
  StringRef hold_args[] = {self, "child-hold"};
  start = now();
  mark("__FINITE_WAIT_8__\n");
  add(llvm::sys::ExecuteAndWait(self, hold_args, std::nullopt, {}, 1, 0, &error, &failed));
  add(failed); text(error); add(now() - start);
  start = now(); timespec duration{2, 0};
  mark("__FINITE_WAIT_CLEANUP__\n");
  add(nanosleep(&duration, nullptr)); add(now() - start);
  errno = 0; add(waitpid(-1, &status, WNOHANG)); add(errno);
#else
  add(alarm(10));
  auto value = Wait(process(blocked), 1, &error, nullptr, true);
  int saved_errno = errno;
  result(value, 0, error); add(saved_errno); add(now() - start); live(blocked);
  add(alarm(0)); add(Caught);
  add(sigaction(SIGALRM, nullptr, &observed));
  add(observed.sa_handler == handler); add(observed.sa_flags);

  pid_t sibling = fork(); if (!sibling) _Exit(29);
  start = now(); errno = 73;
  value = Wait(process(blocked), 1, &error);
  saved_errno = errno;
  result(value, blocked, error); add(saved_errno); add(now() - start); live(blocked);
  int status = -1; errno = 0;
  add(waitpid(blocked, &status, WNOHANG)); add(errno); add(status);
  add(waitpid(sibling, &status, 0) == sibling); add(status); add(close(gate[1]));
  error.clear(); blocked = held(gate); start = now();
  value = Wait(process(blocked), 1, nullptr);
  add(value.Pid == blocked); add(value.ReturnCode); add(now() - start); live(blocked);
  add(close(gate[1]));

  for (unsigned seconds : {1u, UINT32_MAX}) {
    start = now();
    pid_t sleeper = fork(); if (!sleeper) { usleep(20000); _Exit(43); }
    error.clear(); errno = 73;
    value = Wait(process(sleeper), seconds, &error);
    saved_errno = errno;
    result(value, sleeper, error); add(saved_errno); add(now() - start); live(sleeper);
  }
  for (unsigned restart = 0; restart < 2; ++restart) {
    Caught = 0; action.sa_flags = restart ? SA_RESTART : 0;
    add(sigaction(SIGUSR1, &action, nullptr));
    pid_t parent = getpid(), sender = fork();
    if (!sender) { usleep(20000); kill(parent, SIGUSR1); usleep(20000); _Exit(41); }
    error.clear(); start = now(); errno = 73;
    value = Wait(process(sender), 1, &error, nullptr, restart);
    saved_errno = errno;
    result(value, sender, error); add(saved_errno); add(Caught); add(now() - start);
  }
  for (unsigned seconds : {1u, UINT32_MAX}) {
    StringRef args[] = {self, "child-short"};
    error.clear(); bool failed = true; start = now();
    add(llvm::sys::ExecuteAndWait(self, args, std::nullopt, {}, seconds, 0, &error, &failed));
    add(failed); text(error); add(now() - start);
  }
  for (auto mode : {"child-hold", "child-spin"}) {
    StringRef args[] = {self, mode};
    error.clear(); bool failed = true; start = now();
    add(llvm::sys::ExecuteAndWait(self, args, std::nullopt, {}, 1, 0, &error, &failed));
    add(failed); text(error); add(now() - start);
    pid_t child = -1;
    int fd = open("/tmp/finite-wait-child-pid", O_RDONLY);
    add(fd >= 0); add(read(fd, &child, sizeof(child))); add(close(fd));
    live(child);
    errno = 0; add(waitpid(child, &status, WNOHANG)); add(errno);
    errno = 0; add(access("/tmp/finite-wait-survived", F_OK)); add(errno);
  }
  error.clear(); bool failed = true;
  StringRef args[] = {self, "child-short"};
  auto running = llvm::sys::ExecuteNoWait(self, args, std::nullopt, {}, 0,
                                         &error, &failed, nullptr, false);
  add(running.Pid > 0); add(failed); text(error);
  value = Wait(running, 1, &error); result(value, running.Pid, error);
  live(running.Pid);
  error.clear(); errno = 73;
  value = Wait(process(INT32_MAX), 1, &error);
  result(value, INT32_MAX, error); add(errno);
#endif
  return finish();
}
