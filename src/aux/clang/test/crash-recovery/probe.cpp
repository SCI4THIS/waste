// Actual archive-linked LLVM observations; all expectations belong to WAST.
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/Process.h"
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>

using llvm::CrashRecoveryContext;
static uint32_t Values[256], Count;
static void add(uint32_t value) { Values[Count++] = value; }
static volatile unsigned Reached, Cleaned, Destroyed, Order;

class Cleanup final : public llvm::CrashRecoveryContextCleanup {
  unsigned Id;
public:
  Cleanup(CrashRecoveryContext *context, unsigned id)
      : CrashRecoveryContextCleanup(context), Id(id) {}
  ~Cleanup() override { ++Destroyed; }
  void recoverResources() override {
    ++Cleaned;
    Order = Order * 10 + Id;
    add(cleanupFired);
    add(CrashRecoveryContext::isRecoveringFromCrash());
  }
};

static void previous(int signal) { Reached += signal; }
static unsigned SignalCleaned;
static void signal_cleanup(void *) { ++SignalCleaned; }

extern "C" int main(int argc, char **argv, char **env) {
  (void)argc; (void)argv; (void)env;
  struct sigaction original, action = {}, observed;
  action.sa_handler = previous;
  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGUSR2);
  action.sa_flags = SA_RESTART;
  add(sigaction(SIGABRT, &action, &original));
  add(CrashRecoveryContext::GetCurrent() == nullptr);
  add(CrashRecoveryContext::isRecoveringFromCrash());
  {
    CrashRecoveryContext context;
    add(context.RunSafely([&] { ++Reached; add(CrashRecoveryContext::GetCurrent() == nullptr); }));
    add(context.RetCode);
  }
  add(Reached);
  CrashRecoveryContext::Enable();
  CrashRecoveryContext::Enable();
  add(sigaction(SIGABRT, nullptr, &observed));
  add(observed.sa_handler != previous);
  add(observed.sa_flags);
  {
    CrashRecoveryContext context;
    add(context.RunSafely([&] {
      add(CrashRecoveryContext::GetCurrent() == &context);
      add(CrashRecoveryContext::isRecoveringFromCrash());
      context.registerCleanup(new Cleanup(&context, 1));
      context.registerCleanup(new Cleanup(&context, 2));
      auto *removed = new Cleanup(&context, 9);
      context.registerCleanup(removed);
      context.unregisterCleanup(removed);
      add(Destroyed); add(Cleaned);
      ++Reached;
    }));
    add(context.RetCode);
    add(Cleaned);
  }
  add(Cleaned); add(Destroyed); add(Order);
  add(CrashRecoveryContext::GetCurrent() == nullptr);
  add(CrashRecoveryContext::isRecoveringFromCrash());
  for (int signal : {SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV, SIGTRAP}) {
    CrashRecoveryContext context;
    Reached = 0;
    add(context.RunSafely([&] {
      add(CrashRecoveryContext::GetCurrent() == &context);
      raise(signal);
      Reached = 999;
    }));
    add(context.RetCode);
    add(Reached);
    sigset_t mask;
    add(sigprocmask(SIG_BLOCK, nullptr, &mask));
    add(sigismember(&mask, signal));
    add(CrashRecoveryContext::GetCurrent() == nullptr);
  }
  {
    CrashRecoveryContext outer;
    add(outer.RunSafely([&] {
      add(CrashRecoveryContext::GetCurrent() == &outer);
      {
        CrashRecoveryContext inner;
        add(inner.RunSafely([&] { inner.HandleExit(37); Reached = 999; }));
        add(inner.RetCode);
        add(CrashRecoveryContext::GetCurrent() == &outer);
      }
      add(CrashRecoveryContext::GetCurrent() == &outer);
      outer.HandleExit(63);
    }));
    add(outer.RetCode);
  }
  add(CrashRecoveryContext::GetCurrent() == nullptr);
  {
    CrashRecoveryContext context;
    add(context.RunSafely([&] { add(usleep(1000)); context.HandleExit(0); }));
    add(context.RetCode);
  }
  for (int code : {0, 37, 128, 129, 134, 139, 255})
    add(CrashRecoveryContext::isCrash(code));
  add(CrashRecoveryContext::throwIfCrash(37));
  CrashRecoveryContext::Disable();
  CrashRecoveryContext::Disable();
  add(sigaction(SIGABRT, nullptr, &observed));
  add(observed.sa_handler == previous);
  add(observed.sa_flags);
  add(sigismember(&observed.sa_mask, SIGUSR2));
  add(sigaction(SIGABRT, &original, nullptr));
  add(CrashRecoveryContext::GetCurrent() == nullptr);
  Cleaned = Destroyed = Order = 0;
  CrashRecoveryContext::Enable();
  {
    CrashRecoveryContext context;
    add(context.RunSafely([&] {
      context.registerCleanup(new Cleanup(&context, 1));
      context.registerCleanup(new Cleanup(&context, 2));
      llvm::sys::Process::Exit(55);
    }));
    add(context.RetCode); add(Cleaned);
  }
  add(Cleaned); add(Destroyed); add(Order);
  const char *path = "/tmp/llvm-recovery-remove";
  int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
  add(fd >= 0); add(write(fd, "x", 1)); add(close(fd));
  add(llvm::sys::RemoveFileOnSignal(path));
  llvm::sys::AddSignalHandler(signal_cleanup, nullptr);
  {
    CrashRecoveryContext context;
    context.DumpStackAndCleanupOnFailure = true;
    add(context.RunSafely([&] { raise(SIGABRT); Reached = 999; }));
    add(context.RetCode);
  }
  add(SignalCleaned);
  add(access(path, F_OK));
  llvm::sys::DontRemoveFileOnSignal(path);
  CrashRecoveryContext::Disable();
  // Utility setup must retain ignored actions on uninstall, including after
  // an earlier ordinary Enable/Disable cycle recorded a different action.
  struct sigaction fpe_original;
  action.sa_handler = SIG_IGN;
  action.sa_flags = SA_RESTART;
  add(sigaction(SIGFPE, &action, &fpe_original));
  CrashRecoveryContext::Enable(true);
  add(sigaction(SIGFPE, nullptr, &observed));
  add(observed.sa_handler == SIG_IGN);
  add(observed.sa_flags);
  CrashRecoveryContext::Disable();
  add(sigaction(SIGFPE, nullptr, &observed));
  add(observed.sa_handler == SIG_IGN);
  add(observed.sa_flags);
  add(sigaction(SIGFPE, &fpe_original, nullptr));
  // No current context: restore previous actions and re-raise in a child.
  CrashRecoveryContext::Enable();
  pid_t child = fork();
  if (!child) { raise(SIGILL); _exit(99); }
  add(child > 0);
  int status = -1;
  add(waitpid(child, &status, 0) == child); add(status);
  add(CrashRecoveryContext::GetCurrent() == nullptr);
  CrashRecoveryContext::Disable();
  return write(1, Values, Count * sizeof(Values[0])) != (ssize_t)(Count * sizeof(Values[0]));
}
