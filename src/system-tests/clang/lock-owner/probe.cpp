// Record guest queries and lifecycle observations; WAST owns the expectations.
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/WasteLockFile.h"
#include <cstdint>
#include <cstring>
#include <limits>
#include <system_error>
#include <sys/wait.h>

extern "C" void waste_identity_set(unsigned, unsigned, unsigned, unsigned,
                                    char *);
static int32_t Observations[96];
static unsigned Count;
static volatile sig_atomic_t Caught;
static void handler(int) { ++Caught; }
static void record(int32_t Value) { Observations[Count++] = Value; }
using llvm::detail::wasteLockOwnerStillExecuting;

extern "C" int main(int argc, char **argv, char **environment) {
  (void)argc; (void)argv; (void)environment;
  char Host[256] = {};
  const int Self = getpid();
  const int Missing = std::numeric_limits<int>::max();
  record(LLVM_WASTE_GUEST);
  record(LLVM_ON_UNIX);
  record(gethostname(Host, sizeof(Host)));
  record(strcmp(Host, "waste") == 0);

  errno = 17;
  record(kill(Self, 0)); record(errno);
  errno = ESRCH;
  record(wasteLockOwnerStillExecuting(Host, Self)); record(errno);
  errno = 17;
  record(kill(Missing, 0)); record(errno);
  record(wasteLockOwnerStillExecuting(Host, Missing)); record(errno);
  errno = 17;
  record(wasteLockOwnerStillExecuting("another-host", Missing)); record(errno);
  record(wasteLockOwnerStillExecuting(Host, 0)); record(errno);
  record(wasteLockOwnerStillExecuting(Host, -1)); record(errno);
  record(wasteLockOwnerStillExecuting("another-host", -1));
  record(kill(Self, -1)); record(errno);
  record(kill(Self, Missing)); record(errno);
  record(kill(Missing, SIGUSR1)); record(errno);
  errno = 17;
  record(kill(0, 0)); record(errno);
  record(kill(-Missing, 0)); record(errno);
  record(kill(std::numeric_limits<int>::min(), 0)); record(errno);

  struct sigaction Action = {};
  Action.sa_handler = handler;
  sigemptyset(&Action.sa_mask);
  if (sigaction(SIGUSR1, &Action, nullptr) != 0) return 2;
  sigset_t Mask, Previous, Pending;
  record(sigemptyset(&Mask));
  record(sigaddset(&Mask, SIGUSR1));
  record(sigprocmask(SIG_BLOCK, &Mask, &Previous));
  record(raise(SIGUSR1));
  record(sigpending(&Pending));
  record(sigismember(&Pending, SIGUSR1));
  record(wasteLockOwnerStillExecuting(Host, Self));
  record(sigpending(&Pending));
  record(sigismember(&Pending, SIGUSR1));
  record(Caught);
  record(sigprocmask(SIG_SETMASK, &Previous, nullptr));
  record(kill(Self, SIGUSR1));
  record(Caught);

  int Release[2], Report[2];
  int ReleaseResult = pipe(Release), ReportResult = pipe(Report);
  record(ReleaseResult); record(ReportResult);
  if (ReleaseResult || ReportResult) return 2;
  int Child = fork();
  if (Child < 0) return 2;
  if (Child == 0) {
    close(Release[1]); close(Report[0]);
    int32_t ChildReport[4];
    errno = 17;
    ChildReport[0] = kill(Self, 0);
    ChildReport[1] = errno;
    char Byte = 0;
    ChildReport[2] = read(Release[0], &Byte, 1);
    ChildReport[3] = Byte;
    write(Report[1], ChildReport, sizeof(ChildReport));
    // EOF on Report comes from exit, so the parent observes an unreaped zombie.
    _exit(23);
    return 2;
  }
  close(Release[0]); close(Report[1]);
  record(Child > 0);
  errno = 17;
  record(kill(Child, 0)); record(errno);
  record(wasteLockOwnerStillExecuting(Host, Child));
  record(write(Release[1], "R", 1));
  record(close(Release[1]));
  int32_t ChildReport[4] = {};
  record(read(Report[0], ChildReport, sizeof(ChildReport)));
  for (auto Value : ChildReport) record(Value);
  char Byte = 0;
  record(read(Report[0], &Byte, 1));
  close(Report[0]);
  errno = 17;
  record(kill(Child, 0)); record(errno);
  record(wasteLockOwnerStillExecuting(Host, Child));
  int Status = -1;
  record(waitpid(Child, &Status, 0) == Child);
  record(Status);
  record(kill(Child, 0)); record(errno);
  record(wasteLockOwnerStillExecuting(Host, Child)); record(errno);

  // Exercise the actual identity provider's error path, without a fake import.
  char LongHost[300], DefaultHost[] = "waste";
  memset(LongHost, 'x', sizeof(LongHost) - 1);
  LongHost[sizeof(LongHost) - 1] = 0;
  waste_identity_set(0, 0, 0, 0, LongHost);
  record(gethostname(Host, sizeof(Host))); record(errno);
  record(wasteLockOwnerStillExecuting("waste", Self)); record(errno);
  waste_identity_set(0, 0, 0, 0, DefaultHost);
  record(gethostname(Host, sizeof(Host)));
  record(strcmp(Host, "waste") == 0);
  record(static_cast<int>(std::errc::no_such_process));
  return write(1, Observations, Count * sizeof(Observations[0])) !=
         (ssize_t)(Count * sizeof(Observations[0]));
}
