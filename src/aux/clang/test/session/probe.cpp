// Public session observations. Runtime expectations belong entirely to WAST.
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>

static_assert(std::is_same_v<decltype(&setsid), pid_t (*)(void)>);
static_assert(std::is_same_v<decltype(&getsid), pid_t (*)(pid_t)>);
static_assert(std::is_same_v<decltype(&getpgid), pid_t (*)(pid_t)>);
static_assert(std::is_same_v<decltype(&getpgrp), pid_t (*)(void)>);
static_assert(std::is_same_v<decltype(&tcgetpgrp), pid_t (*)(int)>);
extern "C" int raw_setpgid(int, int)
  __attribute__((import_module("env"), import_name("setpgid")));
extern "C" int raw_tcsetpgrp(int, int)
  __attribute__((import_module("env"), import_name("tcsetpgrp")));

static int64_t output[256];
static unsigned count;
static void record(int64_t value) { output[count++] = value; }
static int emit(int fd) {
  return write(fd, output, count * sizeof(output[0])) ==
      static_cast<ssize_t>(count * sizeof(output[0])) ? 0 : 99;
}
static void collect(int fd) {
  unsigned char bytes[1024];
  ssize_t length, total = 0;
  while ((length = read(fd, bytes + total, sizeof(bytes) - total)) > 0)
    total += length;
  record(total);
  if (total > 0 && total % sizeof(int64_t) == 0 &&
      static_cast<unsigned>(total / sizeof(int64_t)) <= 256 - count) {
    memcpy(output + count, bytes, total);
    count += total / sizeof(int64_t);
  }
  close(fd);
}

extern "C" int main(int argc, char **argv, char **) {
  if (argc > 1) {
    const pid_t inherited = atoi(argv[1]);
    count = 0;
    record(getsid(0) == inherited);
    record(getpgid(0) == inherited);
    record(getpgrp() == inherited);
    record(getpid() != inherited);
    record(fcntl(2, F_GETFD) >= 0);
    errno = 0; record(tcgetpgrp(2)); record(errno);
    errno = 73; record(setsid() == getpid()); record(errno);
    record(getsid(0) == getpid()); record(getpgrp() == getpid());
    errno = 0; record(setsid()); record(errno);
    int result = emit(1);
    return result ? result : 19;
  }

  const pid_t parent = getpid(), old_session = getsid(0);
  record(old_session > 0);
  errno = 73; record(getsid(parent) == old_session); record(errno);
  record(getpgid(0) > 0); record(getpgrp() == getpgid(parent));
  errno = 0; record(getsid(-1)); record(errno);
  errno = 0; record(getpgid(-1)); record(errno);
  errno = 0; record(getsid(0x7fffffff)); record(errno);
  errno = 0; record(getpgid(0x7fffffff)); record(errno);
  errno = 0; record(tcgetpgrp(-1)); record(errno);
  int empty[2]; record(pipe(empty));
  errno = 0; record(tcgetpgrp(empty[0])); record(errno);
  close(empty[0]); close(empty[1]);

  // The legacy raw group boundary preserves its positive-pgid convention.
  record(raw_setpgid(0, parent) == parent);
  errno = 0; record(setsid()); record(errno);
  record(getsid(0) == old_session); record(getpgrp() == parent);
  const bool terminal = isatty(2) != 0;
  const pid_t foreground = tcgetpgrp(2);
  record(terminal ? foreground > 0 : foreground == -1);
  int channel[2]; record(pipe(channel));
  pid_t pid = fork();
  if (!pid) {
    close(channel[0]);
    count = 0;
    record(getsid(0) == old_session); record(getpgrp() == parent);
    const int descriptor_flags = fcntl(2, F_GETFD);
    record(descriptor_flags >= 0);
    record(isatty(2) == static_cast<int>(terminal));
    record(terminal ? tcgetpgrp(2) == foreground : tcgetpgrp(2) == -1);
    errno = 73; record(setsid() == getpid()); record(errno);
    record(getsid(0) == getpid()); record(getsid(getpid()) == getpid());
    record(getpgrp() == getpid()); record(getpgid(getpid()) == getpid());
    record(getsid(parent) == old_session); record(getpgid(parent) == parent);
    record(fcntl(2, F_GETFD) == descriptor_flags);
    record(isatty(2) == static_cast<int>(terminal));
    errno = 0; record(tcgetpgrp(2)); record(errno);
    record(raw_tcsetpgrp(2, getpid()) == (terminal ? -ENOTTY : -EINVAL));
    record(raw_setpgid(0, parent)); // Cannot join the old session's group.
    record(raw_setpgid(parent, getpid())); // Cannot mutate its parent session.
    errno = 0; record(setsid()); record(errno);
    record(getsid(0) == getpid()); record(getpgrp() == getpid());

    int nested[2]; record(pipe(nested));
    char identity[24]; snprintf(identity, sizeof(identity), "%d", getpid());
    const pid_t grandchild = fork();
    if (!grandchild) {
      close(nested[0]); dup2(nested[1], 1); close(nested[1]);
      char *next[] = {const_cast<char *>("session-exec"), identity, nullptr};
      execv("/tmp/session-copy.wasm", next);
      _Exit(98);
    }
    close(nested[1]); record(grandchild > 0); collect(nested[0]);
    int status = 0; record(waitpid(grandchild, &status, 0) == grandchild);
    record(status);
    record(getsid(0) == getpid()); record(getpgrp() == getpid());
    int result = emit(channel[1]);
    _Exit(result ? result : 37);
  }
  close(channel[1]); record(pid > 0); collect(channel[0]);
  errno = 73; record(getsid(pid) == pid); record(errno);
  record(getpgid(pid) == pid);
  int status = 0; record(waitpid(pid, &status, 0) == pid); record(status);
  errno = 0; record(getsid(pid)); record(errno);
  errno = 0; record(getpgid(pid)); record(errno);
  record(getsid(0) == old_session); record(getpgrp() == parent);
  record(terminal ? tcgetpgrp(2) == foreground : tcgetpgrp(2) == -1);
  record(isatty(2) == static_cast<int>(terminal));
  return emit(1);
}
