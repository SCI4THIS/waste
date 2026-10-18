// Compiled C/C++ observations. Functional assertions belong to checkpoint.wast.
#include <setjmp.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#ifndef PROBE_JUMP
#define PROBE_JUMP longjmp
#define PROBE_ALIGN _Alignof(jmp_buf)
#define PROBE_CXX 0
#endif
#ifdef __cplusplus
extern "C" unsigned probe_stack_pointer(void);
#else
unsigned probe_stack_pointer(void);
#endif

static unsigned Observations[256], Count;
static sigjmp_buf Shared, Copied;
static volatile unsigned SideEffects, DeepStack, TokenChanged;
static void record(unsigned Value) { Observations[Count++] = Value; }

__attribute__((noinline)) static void deep_jump(jmp_buf Target, int Value,
                                               unsigned Depth) {
  volatile unsigned Scratch[48];
  for (unsigned I = 0; I < 48; ++I)
    Scratch[I] = I + Depth;
  DeepStack = probe_stack_pointer();
  SideEffects += Scratch[0] + 1;
  if (Depth)
    deep_jump(Target, Value, Depth - 1);
  else
    PROBE_JUMP(Target, Value);
}

__attribute__((noinline)) static void temporary_checkpoint(void) {
  switch (sigsetjmp(Shared, 0)) {
  case 0:
    TokenChanged = Shared[0] != Copied[0];
    break;
  default:
    SideEffects = 999;
  }
}

static void observe_value(void) {
  jmp_buf Buffer;
  unsigned Stack = probe_stack_pointer();
  SideEffects = 0;
  switch (setjmp(Buffer)) {
  case 0:
    ++SideEffects;
    deep_jump(Buffer, 7, 3);
    break;
  case 7:
    record(7);
    break;
  default:
    record(999);
  }
  record(SideEffects);
  record(DeepStack < Stack);
  record(probe_stack_pointer() == Stack);
  record(Buffer[0] != 0);
}

static void observe_zero(void) {
  jmp_buf Buffer;
  volatile unsigned Calls = 0;
  unsigned Stack = probe_stack_pointer();
  switch (setjmp(Buffer)) {
  case 0:
    ++Calls;
    deep_jump(Buffer, 0, 1);
    break;
  case 1:
    record(1);
    break;
  default:
    record(999);
  }
  record(Calls);
  record(probe_stack_pointer() == Stack);
}

static void observe_copy(void) {
  unsigned Stack = probe_stack_pointer();
  SideEffects = 0;
  switch (sigsetjmp(Shared, 0)) {
  case 0:
    memcpy(Copied, Shared, sizeof(Shared));
    temporary_checkpoint();
    memcpy(Shared, Copied, sizeof(Shared));
    deep_jump(Shared, -9, 2);
    break;
  case -9:
    record((unsigned)-9);
    break;
  default:
    record(999);
  }
  record(TokenChanged);
  record(SideEffects);
  record(probe_stack_pointer() == Stack);
  record(Shared[0] == Copied[0]);
}

static void observe_signal_api(void) {
  // Use one ordinary user signal; do not enable LLVM crash recovery here.
  sigset_t Set, Previous, Current;
  record(sigemptyset(&Set));
  record(sigaddset(&Set, SIGUSR1));
  record(sigprocmask(SIG_BLOCK, &Set, &Previous));
  record(sigprocmask(SIG_BLOCK, NULL, &Current));
  record(sigismember(&Current, SIGUSR1));
  record(sigprocmask(SIG_SETMASK, &Previous, NULL));
  struct sigaction Action, Original, Observed;
  memset(&Action, 0, sizeof(Action));
  Action.sa_handler = SIG_IGN;
  record(sigemptyset(&Action.sa_mask));
  record(sigaction(SIGUSR1, &Action, &Original));
  record(sigaction(SIGUSR1, NULL, &Observed));
  record(Observed.sa_handler == SIG_IGN);
  record(sigaction(SIGUSR1, &Original, NULL));
}

static void observe_jump_mask(int save_mask) {
  sigjmp_buf Buffer;
  sigset_t Original, Before, Changed, Current;
  sigemptyset(&Before);
  sigaddset(&Before, SIGUSR1);
  sigaddset(&Before, 70);
  sigemptyset(&Changed);
  sigaddset(&Changed, SIGUSR2);
  sigaddset(&Changed, 110);
  record(sigprocmask(SIG_SETMASK, &Before, &Original));
  switch (sigsetjmp(Buffer, save_mask)) {
  case 0:
    record(sigprocmask(SIG_SETMASK, &Changed, NULL));
    record(usleep(1000));
    siglongjmp(Buffer, 7);
    break;
  case 7: record(7); break;
  default: record(999);
  }
  record(sigprocmask(SIG_BLOCK, NULL, &Current));
  record(sigismember(&Current, SIGUSR1));
  record(sigismember(&Current, SIGUSR2));
  record(sigismember(&Current, 70));
  record(sigismember(&Current, 110));
  record(sigprocmask(SIG_SETMASK, &Original, NULL));
}

static sigjmp_buf HandlerTarget;
static volatile unsigned HandlerCalls;
static void jumping_handler(int signal) {
  (void)signal;
  ++HandlerCalls;
  siglongjmp(HandlerTarget, 11);
}
static void counting_handler(int signal) { (void)signal; ++HandlerCalls; }

static void observe_handler_mask(void) {
  struct sigaction Action, Previous;
  sigset_t Original, Before, Empty, Current;
  memset(&Action, 0, sizeof(Action));
  Action.sa_handler = jumping_handler;
  sigemptyset(&Action.sa_mask);
  sigemptyset(&Before); sigaddset(&Before, SIGUSR2);
  sigemptyset(&Empty);
  HandlerCalls = 0;
  record(sigaction(SIGUSR1, &Action, &Previous));
  record(sigprocmask(SIG_SETMASK, &Before, &Original));
  switch (sigsetjmp(HandlerTarget, 1)) {
  case 0:
    record(sigprocmask(SIG_SETMASK, &Empty, NULL));
    raise(SIGUSR1);
    record(999);
    break;
  case 11: record(11); break;
  default: record(999);
  }
  record(HandlerCalls);
  record(sigprocmask(SIG_BLOCK, NULL, &Current));
  record(sigismember(&Current, SIGUSR1));
  record(sigismember(&Current, SIGUSR2));
  record(sigprocmask(SIG_SETMASK, &Original, NULL));
  record(sigaction(SIGUSR1, &Previous, NULL));
}

static void observe_pending_mask(void) {
  sigjmp_buf Buffer;
  struct sigaction Action, Previous;
  sigset_t Original, Empty, Blocked, Current;
  memset(&Action, 0, sizeof(Action));
  Action.sa_handler = counting_handler;
  sigemptyset(&Action.sa_mask);
  sigemptyset(&Empty);
  sigemptyset(&Blocked); sigaddset(&Blocked, SIGUSR1);
  HandlerCalls = 0;
  record(sigaction(SIGUSR1, &Action, &Previous));
  record(sigprocmask(SIG_SETMASK, &Empty, &Original));
  switch (sigsetjmp(Buffer, 1)) {
  case 0:
    record(sigprocmask(SIG_BLOCK, &Blocked, NULL));
    record(raise(SIGUSR1));
    record(HandlerCalls);
    siglongjmp(Buffer, 5);
    break;
  case 5: record(5); break;
  default: record(999);
  }
  // The pending signal may run at an opcode safepoint or this mask boundary.
  // Drain it before starting a fresh sleep, avoiding a timing-specific result.
  record(sigprocmask(SIG_SETMASK, &Empty, &Current));
  errno = 0;
  record(usleep(1000));
  record(errno);
  record(sigismember(&Current, SIGUSR1));
  record(HandlerCalls);
  record(sigprocmask(SIG_SETMASK, &Original, NULL));
  record(sigaction(SIGUSR1, &Previous, NULL));
}

static void observe_fork_mask(void) {
  sigjmp_buf Buffer;
  sigset_t Original, Before, Changed, Current;
  sigemptyset(&Before); sigaddset(&Before, SIGUSR2);
  sigemptyset(&Changed); sigaddset(&Changed, SIGUSR1);
  record(sigprocmask(SIG_SETMASK, &Before, &Original));
  pid_t Child;
  switch (sigsetjmp(Buffer, 1)) {
  case 0:
    Child = fork();
    if (!Child) {
      sigprocmask(SIG_SETMASK, &Changed, NULL);
      siglongjmp(Buffer, 13);
    }
    record(Child > 0);
    int Status;
    record(waitpid(Child, &Status, 0) == Child);
    record(Status);
    break;
  case 13:
    sigprocmask(SIG_BLOCK, NULL, &Current);
    _Exit(sigismember(&Current, SIGUSR2) * 20 + sigismember(&Current, SIGUSR1));
    break;
  default: record(999);
  }
  record(sigprocmask(SIG_BLOCK, NULL, &Current));
  record(sigismember(&Current, SIGUSR1));
  record(sigismember(&Current, SIGUSR2));
  record(sigprocmask(SIG_SETMASK, &Original, NULL));
}

#ifdef __cplusplus
extern "C"
#endif
int main(int argc, char **argv, char **environment) {
  (void)argc; (void)argv; (void)environment;
  record(PROBE_CXX);
  record(sizeof(jmp_buf));
  record(sizeof(sigjmp_buf));
  record(PROBE_ALIGN);
  const unsigned Codes[] = {EX_OK, EX__BASE, EX_USAGE, EX_DATAERR, EX_NOINPUT,
      EX_NOUSER, EX_NOHOST, EX_UNAVAILABLE, EX_SOFTWARE, EX_OSERR, EX_OSFILE,
      EX_CANTCREAT, EX_IOERR, EX_TEMPFAIL, EX_PROTOCOL, EX_NOPERM, EX_CONFIG,
      EX__MAX};
  for (unsigned I = 0; I < sizeof(Codes) / sizeof(Codes[0]); ++I)
    record(Codes[I]);
  observe_value();
  observe_zero();
  observe_copy();
  observe_signal_api();
  observe_jump_mask(0);
  observe_jump_mask(1);
  observe_jump_mask(-1);
  observe_handler_mask();
  observe_pending_mask();
  observe_fork_mask();
  return write(1, Observations, Count * sizeof(Observations[0])) !=
         (ssize_t)(Count * sizeof(Observations[0]));
}
