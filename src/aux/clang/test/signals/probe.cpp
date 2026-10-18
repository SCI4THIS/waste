// Observe public signal registration and actual LLVM methods. WAST owns assertions.
#include "llvm/Config/config.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/ManagedStatic.h"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>

static_assert(sizeof(struct sigaction) == 24);
static_assert(offsetof(struct sigaction, sa_flags) == 20);
static_assert(std::is_same_v<decltype(&signal), sighandler_t (*)(int, sighandler_t)>);
static_assert(std::is_same_v<decltype(&sigaction), int (*)(int, const struct sigaction *, struct sigaction *)>);
extern "C" void signals_fixture_register_utility();
static int64_t output[256];
static unsigned count;
static volatile sig_atomic_t caught, info, interrupted, piped;
static void record(int64_t value) { output[count++] = value; }
static void handler(int sig) { caught += sig; }
static void info_handler() { ++info; errno = 99; }
static void interrupt_handler() { ++interrupted; }
static void pipe_handler() { ++piped; }
static void touch(const char *path) {
  int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
  if (fd >= 0) { write(fd, "x", 1); close(fd); }
}
static bool exists(const char *path) { struct stat st; return stat(path, &st) == 0; }
static void action_is(int sig, sighandler_t fn, int flags) {
  struct sigaction observed = {};
  record(sigaction(sig, nullptr, &observed));
  record(observed.sa_handler == fn); record(observed.sa_flags == flags);
}
static int emit(int fd) {
  return write(fd, output, count * sizeof(output[0])) ==
      static_cast<ssize_t>(count * sizeof(output[0])) ? 0 : 99;
}

extern "C" int main(int, char **, char **) {
  record(LLVM_WASTE_GUEST); record(LLVM_ENABLE_THREADS);
  struct sigaction original = {}, action = {}, old = {};
  record(sigaction(SIGUSR1, nullptr, &original));
  errno = 73; record(signal(SIGUSR1, handler) == original.sa_handler); record(errno);
  action_is(SIGUSR1, handler, SA_RESTART);
  record(raise(SIGUSR1)); record(caught);
  record(raise(SIGUSR1)); record(caught);
  record(signal(SIGUSR1, SIG_IGN) == handler);
  record(raise(SIGUSR1)); record(caught);
  record(signal(SIGUSR1, handler) == SIG_IGN);
  errno = 0; record(signal(0, handler) == SIG_ERR); record(errno);
  errno = 0; record(signal(SIGKILL, handler) == SIG_ERR); record(errno);
  errno = 0; record(signal(SIGUSR1, SIG_ERR) == SIG_ERR); record(errno);
  action_is(SIGUSR1, handler, SA_RESTART);

  action.sa_handler = handler; action.sa_flags = SA_RESTART;
  sigemptyset(&action.sa_mask); sigaddset(&action.sa_mask, SIGUSR2);
  record(sigaction(SIGUSR1, &action, &old));
  action.sa_flags |= 4; // Unavailable flags must fail, not advertise SA_SIGINFO.
  memset(&old, 0x5a, sizeof(old));
  errno = 0; record(sigaction(SIGUSR1, &action, &old)); record(errno);
  bool unchanged = true;
  for (auto byte : reinterpret_cast<unsigned char (&)[24]>(old)) unchanged &= byte == 0x5a;
  record(unchanged);
  record(sigaction(SIGUSR1, nullptr, &old));
  record(old.sa_handler == handler); record(old.sa_flags == SA_RESTART);
  record(sigismember(&old.sa_mask, SIGUSR2));
  const auto bad = reinterpret_cast<struct sigaction *>(UINTPTR_MAX - 7);
  errno = 0; record(sigaction(SIGUSR1, bad, nullptr)); record(errno);
  action.sa_flags = 0;
  errno = 0; record(sigaction(SIGUSR1, &action, bad)); record(errno);
  action_is(SIGUSR1, handler, SA_RESTART);
  record(sigaction(SIGUSR1, &action, &action)); // Input/output alias.
  record(action.sa_handler == handler); record(action.sa_flags == SA_RESTART);
  record(sigismember(&action.sa_mask, SIGUSR2));
  record(sigaction(SIGUSR1, &original, nullptr));

  // Info callbacks persist and preserve errno; unregister restores the original
  // handler, flags and mask exactly even though SIGINFO aliases SIGUSR1.
  record(sigaction(SIGUSR1, &action, nullptr));
  llvm::sys::SetInfoSignalFunction(info_handler);
  errno = 73; record(raise(SIGUSR1)); record(info); record(errno);
  record(raise(SIGINFO)); record(info); record(errno);
  llvm::sys::CleanupOnSignal(SIGUSR1); record(info); record(errno);
  llvm::sys::unregisterHandlers();
  action_is(SIGUSR1, handler, SA_RESTART);
  record(sigaction(SIGUSR1, nullptr, &old)); record(sigismember(&old.sa_mask, SIGUSR2));
  record(sigaction(SIGUSR1, &original, nullptr));

  // Real file ownership: erase/reuse, regular-only cleanup, persistence of
  // cancelled names, and direct interrupt cleanup without callback invocation.
  const char *removed = "/tmp/llvm-signals-remove";
  const char *retained = "/tmp/llvm-signals-retain";
  const char *reused = "/tmp/llvm-signals-reused";
  const char *directory = "/tmp/llvm-signals-dir";
  touch(removed); touch(retained); touch(reused); mkdir(directory, 0700);
  record(llvm::sys::RemoveFileOnSignal(removed));
  record(llvm::sys::RemoveFileOnSignal(retained));
  llvm::sys::DontRemoveFileOnSignal(retained);
  record(llvm::sys::RemoveFileOnSignal(reused));
  record(llvm::sys::RemoveFileOnSignal(directory));
  llvm::sys::SetInterruptFunction(interrupt_handler);
  record(raise(SIGINT)); record(interrupted);
  record(exists(removed)); record(exists(retained)); record(exists(reused)); record(exists(directory));
  action_is(SIGUSR1, original.sa_handler, original.sa_flags);
  llvm::sys::unregisterHandlers();
  touch(removed); llvm::sys::RunInterruptHandlers(); record(exists(removed)); record(interrupted);

  llvm::sys::SetOneShotPipeSignalFunction(pipe_handler);
  record(raise(SIGPIPE)); record(piped);
  struct sigaction pipe_action = {};
  record(sigaction(SIGPIPE, nullptr, &pipe_action)); record(pipe_action.sa_handler == SIG_DFL);

  // Utility setup must leave ignored dispositions alone and restore installed
  // actions. Terminating handlers re-raise the explicit guest signal, with the
  // parent remaining alive and child status retaining the signal number.
  record(signal(SIGTERM, SIG_IGN) != SIG_ERR);
  signals_fixture_register_utility();
  action_is(SIGTERM, SIG_IGN, SA_RESTART);
  llvm::sys::unregisterHandlers();
  action_is(SIGTERM, SIG_IGN, SA_RESTART);
  record(signal(SIGTERM, SIG_DFL) == SIG_IGN);
  const int signals[] = {SIGTERM, SIGSEGV, SIGUSR1, SIGPIPE};
  for (int sig : signals) {
    int channel[2]; record(pipe(channel));
    pid_t pid = fork();
    if (!pid) {
      close(channel[0]);
      if (sig == SIGPIPE) {
        llvm::sys::SetOneShotPipeSignalFunction(llvm::sys::DefaultOneShotPipeSignalHandler);
      } else {
        llvm::sys::SetInfoSignalFunction(info_handler);
        llvm::sys::unregisterHandlers();
        signals_fixture_register_utility();
      }
      raise(sig);
      write(channel[1], "unexpected", 10);
      _Exit(98);
    }
    close(channel[1]); record(pid > 0);
    char byte; record(read(channel[0], &byte, 1)); close(channel[0]);
    int status = 0; record(waitpid(pid, &status, 0) == pid); record(status);
    record(kill(pid, 0)); // Reaped child is absent.
  }
  llvm::sys::unregisterHandlers();
  unlink(retained); rmdir(directory);
  llvm::llvm_shutdown();
  return emit(1);
}
