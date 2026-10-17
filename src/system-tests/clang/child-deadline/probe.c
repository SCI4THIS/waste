/* Ordinary shared-libc/process observations. All expectations live in WAST. */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <waste/process.h>

extern pid_t raw_wait_until(pid_t, int *, int, uint64_t)
  __attribute__((import_module("waste_kernel"), import_name("waitpid_until_v1")));
static int64_t output[256];
static unsigned count;
static volatile sig_atomic_t caught;
static void record(int64_t value) { output[count++] = value; }
static void handler(int signal) { (void)signal; ++caught; }
static uint64_t now(void) {
  struct timespec value;
  clock_gettime(CLOCK_MONOTONIC, &value);
  return (uint64_t)value.tv_sec * 1000000000u + value.tv_nsec;
}
#ifdef WASTE_CHILD_DEADLINE_CONTROLLED
static void mark(const char *text) { write(2, text, strlen(text)); }
#endif

int main(int argc, char **argv, char **environment) {
  (void)argc; (void)argv; (void)environment;
  int gate[2], status = 0x12345678;
  record(pipe(gate));
  pid_t blocked = fork();
  if (!blocked) {
    close(gate[1]);
    char byte;
    read(gate[0], &byte, 1);
    _exit(23);
  }
  close(gate[0]);
  record(blocked > 0);

#ifdef WASTE_CHILD_DEADLINE_CONTROLLED
  struct sigaction action = {0}, observed = {0};
  action.sa_handler = handler;
  sigemptyset(&action.sa_mask);
  record(sigaction(SIGALRM, &action, 0));
  uint64_t start = now();
  errno = 73;
  mark("__CHILD_DEADLINE_1__\n");
  record(waste_waitpid_until(blocked, &status, 0, start + 1000000000));
  record(status); record(errno); record(now() - start); record(kill(blocked, 0));

  start = now();
  uint64_t deadline = start + 3000000000;
  record(alarm(1));
  errno = 73;
  mark("__CHILD_DEADLINE_2__\n");
  record(waste_waitpid_until(blocked, &status, 0, deadline));
  record(status); record(errno); record(caught); record(now() - start);
  mark("__CHILD_DEADLINE_RETRY__\n");
  errno = 73;
  record(waste_waitpid_until(blocked, &status, 0, deadline));
  record(status); record(errno); record(now() - start); record(alarm(0));

  caught = 0;
  action.sa_flags = SA_RESTART;
  record(sigaction(SIGALRM, &action, 0));
  start = now(); record(alarm(1)); errno = 73;
  mark("__CHILD_DEADLINE_3__\n");
  record(waste_waitpid_until(blocked, &status, 0, start + 3000000000));
  record(status); record(errno); record(caught); record(now() - start);
  record(sigaction(SIGALRM, 0, &observed));
  record(observed.sa_handler == handler); record(observed.sa_flags == SA_RESTART);

  caught = 0;
  sigset_t mask, pending;
  sigemptyset(&mask); sigaddset(&mask, SIGALRM);
  record(sigprocmask(SIG_BLOCK, &mask, 0));
  start = now(); record(alarm(1));
  mark("__CHILD_DEADLINE_4__\n");
  record(waste_waitpid_until(blocked, &status, 0, start + 2000000000));
  record(status); record(caught); record(now() - start);
  record(sigpending(&pending)); record(sigismember(&pending, SIGALRM));
  record(sigprocmask(SIG_UNBLOCK, &mask, 0)); record(caught);

  start = now(); record(alarm(3));
  mark("__CHILD_DEADLINE_5__\n");
  record(waste_waitpid_until(blocked, &status, 0, start + 1000000000));
  record(status); record(now() - start); record(alarm(0));

  caught = 0; action.sa_flags = 0;
  record(sigaction(SIGALRM, &action, 0));
  start = now(); record(alarm(1)); errno = 73;
  mark("__CHILD_DEADLINE_6__\n");
  record(waste_waitpid_until(blocked, &status, 0, UINT64_MAX));
  record(status); record(errno); record(caught); record(now() - start);
  record(write(gate[1], "R", 1)); record(close(gate[1]));
  record(waste_waitpid_until(blocked, &status, 0, now() + 1000000000) == blocked);
  record(status);
  start = now();
  struct timespec duration = {2, 0};
  mark("__CHILD_DEADLINE_CLEANUP__\n");
  record(nanosleep(&duration, 0)); record(now() - start);
  errno = 0; record(waste_waitpid_until(blocked, &status, 0, UINT64_MAX));
  record(errno); record(status);
#else
  /* Expiry and nonblocking queries preserve status/errno and never kill. */
  for (unsigned options = 0; options <= WNOHANG; ++options) {
    errno = 73;
    record(waste_waitpid_until(blocked, &status, options,
                              options ? UINT64_MAX : 0));
    record(status); record(errno);
  }
  const int invalid_options[] = {2, -1};
  for (unsigned i = 0; i < sizeof(invalid_options) / sizeof(*invalid_options); ++i) {
    errno = 0;
    record(waste_waitpid_until(blocked, &status, invalid_options[i], UINT64_MAX));
    record(errno); record(status);
  }
  errno = 73; record(raw_wait_until(blocked, &status, 2, UINT64_MAX));
  record(errno); record(status);
  const pid_t invalid_pids[] = {-2, INT32_MAX};
  for (unsigned i = 0; i < sizeof(invalid_pids) / sizeof(*invalid_pids); ++i) {
    errno = 0; record(waste_waitpid_until(invalid_pids[i], &status, 0, 0));
    record(errno); record(status);
  }
  errno = 0;
  record(waste_waitpid_until(blocked, (int *)(uintptr_t)UINTPTR_MAX, 0, 0));
  record(errno); record(kill(blocked, 0));
  uint64_t start = now(); errno = 73;
  record(waste_waitpid_until(blocked, &status, 0, start + 20000000));
  record(status); record(errno); record(now() - start); record(kill(blocked, 0));

  /* A ready sibling cannot satisfy a positive-PID wait or lose its status. */
  pid_t sibling = fork(); if (!sibling) _exit(29);
  start = now();
  record(waste_waitpid_until(blocked, &status, 0, start + 10000000));
  record(status); record(now() - start);
  record(waste_waitpid_until(-1, &status, 0, 0) == sibling); record(status);
  record(write(gate[1], "R", 1)); record(close(gate[1]));
  errno = 73;
  record(waste_waitpid_until(blocked, &status, 0, now() + 1000000000) == blocked);
  record(status); record(errno);
  errno = 0; record(waste_waitpid_until(0, &status, 0, 0));
  record(errno); record(status);
  pid_t discarded = fork(); if (!discarded) _exit(24);
  errno = 73;
  record(waste_waitpid_until(discarded, 0, 0, 0) == discarded); record(errno);
  pid_t sleeper = fork(); if (!sleeper) { usleep(10000); _exit(25); }
  start = now();
  record(waste_waitpid_until(sleeper, &status, 0, start + 1000000000) == sleeper);
  record(status); record(now() - start);

  /* Validate the complete output before consuming a zombie, on every retry. */
  pid_t zombie = fork(); if (!zombie) _exit(26);
  size_t page = getpagesize();
  unsigned char *mapping = mmap(0, page * 2, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  record(mapping != MAP_FAILED);
  if (mapping == MAP_FAILED) return 99;
  memset(mapping, 0xa5, page * 2);
  record(mprotect(mapping, page, PROT_READ));
  errno = 0;
  record(waste_waitpid_until(zombie, (int *)(mapping + 8), 0, 0));
  record(errno); record(kill(zombie, 0)); record(*(uint32_t *)(mapping + 8));
  record(mprotect(mapping, page, PROT_READ | PROT_WRITE));
  record(mprotect(mapping + page, page, PROT_READ));
  errno = 0;
  record(waste_waitpid_until(zombie, (int *)(mapping + page - 2), 0, 0));
  record(errno); record(kill(zombie, 0));
  record(*(uint32_t *)(mapping + page - 4)); record(*(uint32_t *)(mapping + page));
  record(munmap(mapping + page, page));
  errno = 0;
  record(waste_waitpid_until(zombie, (int *)(mapping + page + 8), 0, 0));
  record(errno); record(kill(zombie, 0));
  record(waste_waitpid_until(zombie, &status, 0, 0) == zombie); record(status);
  record(munmap(mapping, page));

  for (unsigned restart = 0; restart < 2; ++restart) {
    caught = 0;
    struct sigaction action = {0};
    action.sa_handler = handler; sigemptyset(&action.sa_mask);
    action.sa_flags = restart ? SA_RESTART : 0;
    record(sigaction(SIGUSR1, &action, 0));
    pid_t parent = getpid(), sender = fork();
    if (!sender) { usleep(20000); kill(parent, SIGUSR1); usleep(30000); _exit(27); }
    start = now(); uint64_t deadline = start + 1000000000;
    status = 0x12345678; errno = 73;
    pid_t result = waste_waitpid_until(sender, &status, 0, deadline);
    record(result == sender ? 1 : result); record(errno); record(status); record(caught);
    if (!restart) {
      record(waste_waitpid_until(sender, &status, 0, deadline) == sender); record(status);
    }
    record(now() - start);
  }

  /* Two independently waiting parents retain their own child/deadline state. */
  int reports[2][2]; pid_t watchers[2];
  for (unsigned i = 0; i < 2; ++i) {
    record(pipe(reports[i]));
    watchers[i] = fork();
    if (!watchers[i]) {
      close(reports[i][0]);
      int hold[2]; pipe(hold);
      pid_t nested = fork();
      if (!nested) { close(hold[1]); char byte; read(hold[0], &byte, 1); _exit(98); }
      close(hold[0]);
      int child_status = 0x12345678;
      uint64_t begin = now();
      int64_t values[6];
      values[0] = waste_waitpid_until(nested, &child_status, 0,
                                    begin + (i + 1) * 20000000);
      values[1] = child_status; values[2] = now() - begin;
      values[3] = kill(nested, SIGKILL);
      values[4] = waitpid(nested, &child_status, 0) == nested;
      values[5] = child_status;
      write(reports[i][1], values, sizeof(values));
      _exit(30 + i);
    }
    close(reports[i][1]);
  }
  for (unsigned i = 0; i < 2; ++i) {
    record(waste_waitpid_until(watchers[i], &status, 0, now() + 1000000000) == watchers[i]);
    record(status);
    int64_t values[6] = {0};
    record(read(reports[i][0], values, sizeof(values)));
    for (unsigned j = 0; j < 6; ++j) record(values[j]);
    record(close(reports[i][0]));
  }
#endif
  return write(1, output, count * sizeof(*output)) != (ssize_t)(count * sizeof(*output));
}
