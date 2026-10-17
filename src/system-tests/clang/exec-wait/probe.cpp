// Public process calls and ordinary child observations; WAST owns expectations.
#include "llvm/Config/config.h"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>

static_assert(std::is_same_v<decltype(&execv), int (*)(const char *, char *const *)>);
static_assert(std::is_same_v<decltype(&wait), pid_t (*)(int *)>);
extern "C" pid_t raw_waitpid(pid_t, int *, int)
  __attribute__((import_module("env"), import_name("waitpid")));

static int64_t output[256];
static unsigned count;
static volatile sig_atomic_t caught;
static void handler(int) { ++caught; }
static void record(int64_t value) { output[count++] = value; }
static void text(const char *value) {
  int64_t words[4] = {};
  if (value) {
    size_t length = strlen(value);
    memcpy(words, value, length < sizeof(words) ? length : sizeof(words));
  }
  for (auto word : words) record(word);
}
static int64_t word(const void *pointer) {
  int64_t value; memcpy(&value, pointer, sizeof(value)); return value;
}
static int child(int argc, char **argv, char **environment) {
  record(argc); record(getpid());
  int entries = 0; while (environment && environment[entries]) ++entries;
  record(entries);
  text(argv[0]); text(argv[2]); text(getenv("WASTE_EXEC_MARK"));
  text(getenv("PATH")); text(getenv("EMPTY"));
  record(getenv("EMPTY") != nullptr);
  int keep = atoi(argv[3]), closed = atoi(argv[4]);
  errno = 73; record(fcntl(keep, F_GETFD)); record(errno);
  char byte = 0; record(read(keep, &byte, 1)); record(byte);
  errno = 0; record(fcntl(closed, F_GETFD)); record(errno);
  return write(1, output, count * sizeof(output[0])) ==
         static_cast<ssize_t>(count * sizeof(output[0])) ? 37 : 99;
}
static void reaped(pid_t pid, int status, int result) {
  record(pid > 0); record(result == pid); record(status);
}

extern "C" int main(int argc, char **argv, char **environment) {
  if (argc > 1 && strcmp(argv[1], "child") == 0)
    return child(argc, argv, environment);
  record(LLVM_WASTE_GUEST); record(LLVM_ENABLE_THREADS);
  char **saved = environ;
  char mark[] = "WASTE_EXEC_MARK=inherited";
  char path[] = "PATH=/tmp";
  char empty[] = "EMPTY=";
  char *inherited[] = {mark, path, empty, nullptr};
  char *none[] = {nullptr};
  char replaced[] = "WASTE_EXEC_MARK=replaced";
  char *replacement[] = {replaced, nullptr};
  char *arguments[] = {const_cast<char *>("untrusted-zero"), nullptr};
  environ = inherited;
  // Exact paths do not search PATH; failing calls preserve this process image.
  record(chdir("/"));
  const char *failures[] = {nullptr, reinterpret_cast<const char *>(UINTPTR_MAX),
                           "/tmp/exec-wait-missing", "exec-wait-copy.wasm"};
  for (auto file : failures) {
    errno = 0; record(execv(file, arguments)); record(errno);
  }
  char *const *invalid_vectors[] = {nullptr,
      reinterpret_cast<char *const *>(UINTPTR_MAX - 1)};
  for (auto invalid : invalid_vectors) {
    errno = 0; record(execv("/tmp/exec-wait-copy.wasm", invalid)); record(errno);
  }
  environ = reinterpret_cast<char **>(UINTPTR_MAX - 1);
  errno = 0; record(execv("/tmp/exec-wait-copy.wasm", arguments)); record(errno);
  environ = inherited;
  text(getenv("WASTE_EXEC_MARK"));

  // Successful replacement retains PID/descriptors, copies argv and selects env.
  for (unsigned mode = 0; mode < 3; ++mode) {
    environ = mode == 1 ? none : inherited;
    int keep = open("/tmp/exec-wait-kept", O_CREAT | O_TRUNC | O_RDWR, 0600);
    record(keep >= 0); record(write(keep, "K", 1)); record(lseek(keep, 0, SEEK_SET));
    int closed = dup(keep); record(closed >= 0); record(fcntl(closed, F_SETFD, FD_CLOEXEC));
    int channel[2]; record(pipe(channel));
    char keep_arg[16], closed_arg[16];
    snprintf(keep_arg, sizeof(keep_arg), "%d", keep);
    snprintf(closed_arg, sizeof(closed_arg), "%d", closed);
    pid_t pid = fork();
    if (!pid) {
      close(channel[0]); dup2(channel[1], STDOUT_FILENO); close(channel[1]);
      char *next[] = {const_cast<char *>("untrusted-zero"), const_cast<char *>("child"),
                     const_cast<char *>("two words"), keep_arg, closed_arg, nullptr};
      if (mode == 2) execve("/tmp/exec-wait-copy.wasm", next, replacement);
      else {
        if (mode == 1) chdir("/tmp");
        execv(mode == 1 ? "exec-wait-copy.wasm" : "/tmp/exec-wait-copy.wasm", next);
      }
      _Exit(98);
    }
    close(channel[1]);
    int status = -1; errno = 73;
    int result = wait(&status), waited_errno = errno;
    reaped(pid, status, result); record(waited_errno);
    int64_t observed[30] = {};
    record(read(channel[0], observed, sizeof(observed)));
    record(observed[1] == pid);
    for (unsigned index = 0; index < 30; ++index)
      if (index != 1) record(observed[index]);
    record(read(channel[0], observed, sizeof(observed)));
    record(close(channel[0]));
    record(fcntl(closed, F_GETFD)); record(close(closed)); record(close(keep));
  }
  environ = saved;

  // Any-child waiting reaps a ready sibling before a blocked child.
  int gate[2]; record(pipe(gate));
  pid_t slow = fork();
  if (!slow) { close(gate[1]); char byte; read(gate[0], &byte, 1); _Exit(31); }
  close(gate[0]);
  int status = 0x12345678; errno = 73;
  record(waitpid(slow, &status, WNOHANG)); record(errno); record(status);
  pid_t fast = fork();
  if (!fast) { close(gate[1]); _Exit(32); }
  record(fast > 0); status = -1;
  int result = wait(&status); record(result == fast); record(status);
  record(write(gate[1], "R", 1)); record(close(gate[1]));
  status = -1; result = wait(&status); reaped(slow, status, result);

  // A zombie survives invalid/protected/partial output destinations.
  int done[2]; record(pipe(done));
  pid_t zombie = fork();
  if (!zombie) { close(done[0]); write(done[1], "Z", 1); _Exit(44); }
  close(done[1]); char byte; record(read(done[0], &byte, 1)); record(byte);
  record(read(done[0], &byte, 1)); record(close(done[0]));
  size_t page = getpagesize(); record(page);
  auto *mapping = static_cast<unsigned char *>(mmap(nullptr, page * 2,
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  record(mapping != MAP_FAILED);
  if (mapping == MAP_FAILED) return 99;
  memset(mapping, 0xa5, page * 2);
  int *bad[] = {reinterpret_cast<int *>(UINTPTR_MAX), reinterpret_cast<int *>(UINTPTR_MAX - 1)};
  for (auto pointer : bad) {
    errno = 0; record(wait(pointer)); record(errno); record(kill(zombie, 0));
  }
  record(mprotect(mapping, page, PROT_READ));
  errno = 0; record(raw_waitpid(zombie, reinterpret_cast<int *>(mapping + 8), 0));
  record(errno); record(kill(zombie, 0)); record(word(mapping + 8));
  record(mprotect(mapping, page, PROT_READ | PROT_WRITE));
  record(mprotect(mapping + page, page, PROT_READ));
  errno = 0; record(wait(reinterpret_cast<int *>(mapping + page - 2)));
  record(errno); record(kill(zombie, 0));
  record(word(mapping + page - 8)); record(word(mapping + page));
  record(munmap(mapping + page, page));
  errno = 0; record(raw_waitpid(zombie, reinterpret_cast<int *>(mapping + page + 8), 0));
  record(errno); record(kill(zombie, 0));
  status = -1; result = wait(&status); reaped(zombie, status, result);
  record(munmap(mapping, page));

  // NULL status is valid; a write-only fresh page needs no read permission.
  pid_t discarded = fork(); if (!discarded) _Exit(45);
  errno = 73; result = wait(nullptr); record(discarded > 0); record(result == discarded); record(errno);
  auto *write_only = static_cast<int *>(mmap(nullptr, page, PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  record(write_only != MAP_FAILED);
  pid_t writer = fork(); if (!writer) _Exit(46);
  errno = 73; result = raw_waitpid(writer, write_only, 0);
  record(writer > 0); record(result == writer); record(errno);
  record(mprotect(write_only, page, PROT_READ | PROT_WRITE));
  record(write_only == MAP_FAILED ? -1 : *write_only); record(munmap(write_only, page));

  // Signals interrupt or restart the same shared WAITPID continuation.
  for (unsigned restart = 0; restart < 2; ++restart) {
    caught = 0;
    struct sigaction action = {}, old = {};
    action.sa_handler = handler; sigemptyset(&action.sa_mask);
    action.sa_flags = restart ? SA_RESTART : 0;
    record(sigaction(SIGUSR1, &action, &old));
    pid_t parent = getpid(), sender = fork();
    if (!sender) { usleep(20000); kill(parent, SIGUSR1); usleep(30000); _Exit(43); }
    status = 0x12345678; errno = 73;
    result = wait(&status); record(result == sender ? 1 : result); record(errno);
    record(status); record(caught);
    if (!restart) { result = wait(&status); record(result == sender); record(status); }
    record(sigaction(SIGUSR1, &old, nullptr));
  }
  status = 0x12345678; errno = 0;
  record(wait(&status)); record(errno); record(status);
  record(getpid() > 0);
  return write(1, output, count * sizeof(output[0])) !=
         static_cast<ssize_t>(count * sizeof(output[0]));
}
