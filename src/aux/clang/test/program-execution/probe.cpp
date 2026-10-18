// Observe the actual LLVMSupport archive through ordinary CRT/C++/libc.
// Runtime expectations belong exclusively to program-execution.wast.
#include "llvm/Config/config.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/Support/Program.h"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

using llvm::StringRef;
using llvm::sys::ProcessInfo;
static uint64_t Output[512];
static unsigned Count;
static void add(int64_t value) { Output[Count++] = static_cast<uint64_t>(value); }
static void text(const char *bytes, size_t length) {
  add(length);
  unsigned char record[64] = {};
  memcpy(record, bytes, length < sizeof record ? length : sizeof record);
  memcpy(Output + Count, record, sizeof record);
  Count += sizeof record / sizeof Output[0];
}
static void text(const std::string &value) { text(value.data(), value.size()); }
static void file(const char *path) {
  char bytes[64] = {};
  int fd = open(path, O_RDONLY);
  ssize_t size = fd < 0 ? -1 : read(fd, bytes, sizeof bytes);
  add(size);
  memcpy(Output + Count, bytes, sizeof bytes);
  Count += sizeof bytes / sizeof Output[0];
  if (fd >= 0) close(fd);
}
static void create(const char *path, const char *bytes, unsigned mode = 0644) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
  if (fd >= 0) { write(fd, bytes, strlen(bytes)); close(fd); }
}
static uint64_t now() {
  timespec value = {};
  clock_gettime(CLOCK_MONOTONIC, &value);
  return uint64_t(value.tv_sec) * 1000000000 + value.tv_nsec;
}
static void line(const char *value) {
  if (value) write(1, value, strlen(value));
  else write(1, "(unset)", 7);
  write(1, "\n", 1);
}

static int child(int argc, char **argv) {
  const char *mode = argv[1];
  if (!strcmp(mode, "args")) {
    add(argc);
    write(1, Output, Count * sizeof Output[0]);
    line(argv[0]); line(argv[2]); line(argv[3]);
    line(getenv("LLVM_EXECUTION_SAMPLE"));
    line(getenv("SHOULD_NOT_BE_INHERITED"));
    return 11;
  }
  if (!strcmp(mode, "streams")) {
    write(1, "OUT\n", 4); write(2, "ERR\n", 4);
    return 12;
  }
  if (!strcmp(mode, "input")) {
    char bytes[64];
    ssize_t size = read(0, bytes, sizeof bytes);
    if (size > 0) write(1, bytes, size);
    return 13;
  }
  if (!strcmp(mode, "hold")) {
    char byte;
    read(atoi(argv[2]), &byte, 1);
    return 37;
  }
  if (!strcmp(mode, "sleep")) {
    usleep(strtoul(argv[2], nullptr, 10));
    return atoi(argv[3]);
  }
  if (!strcmp(mode, "session")) {
    add(getsid(0) == getpid()); add(getpgrp() == getpid());
    add(isatty(0));
    write(1, Output, Count * sizeof Output[0]);
    return 14;
  }
  if (!strcmp(mode, "null")) {
    char bytes[64];
    struct stat metadata = {};
    add(read(0, bytes, sizeof bytes));
    add(write(1, "OUT\n", 4)); add(write(2, "ERR\n", 4));
    add(fstat(0, &metadata)); add(metadata.st_mode & S_IFMT);
    add(metadata.st_size); add(getsid(0) == getpid()); add(isatty(0));
    int fd = open("/tmp/execution-null-child", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) { write(fd, Output, Count * sizeof Output[0]); close(fd); }
    return 18;
  }
  return 15;
}

extern "C" int main(int argc, char **argv, char **environment) {
  if (argc > 1) return child(argc, argv);
  add(LLVM_WASTE_GUEST);
#ifdef HAVE_POSIX_SPAWN
  add(1);
#else
  add(0);
#endif
  pid_t original_pid = getpid(), original_sid = getsid(0);
  std::string error;
  bool failed;
  auto execute = [&](StringRef program, llvm::ArrayRef<StringRef> args,
                     std::optional<llvm::ArrayRef<StringRef>> env = std::nullopt,
                     llvm::ArrayRef<std::optional<StringRef>> redirects = {}) {
    error.clear(); failed = true;
    add(llvm::sys::ExecuteAndWait(program, args, env, redirects, 0, 0,
                                  &error, &failed));
    add(failed); text(error);
  };
  StringRef true_args[] = {"/usr/bin/true"};
  StringRef false_args[] = {"/usr/bin/false"};
  execute(true_args[0], true_args);
  execute(false_args[0], false_args);
  StringRef start_args[] = {"/tmp/program-execution-start.wat"};
  execute(start_args[0], start_args);

  // Views need not end in NUL: the real StringSaver must own each copy.
  StringRef self(argv[0]);
  StringRef arg_storage("a b $ ignored"), env_storage("LLVM_EXECUTION_SAMPLE=value=literal ignored");
  StringRef args[] = {"renamed", "args", arg_storage.substr(0, 6), ""};
  StringRef custom_env[] = {env_storage.substr(0, 35)};
  std::optional<StringRef> redirects[] = {std::nullopt, "/tmp/execution-args", std::nullopt};
  execute(self, args, llvm::ArrayRef<StringRef>(custom_env), redirects);
  file("/tmp/execution-args");
  // Inheritance uses the current shared libc environ.
  char inherited_value[] = "LLVM_EXECUTION_SAMPLE=inherited";
  char inherited_other[] = "SHOULD_NOT_BE_INHERITED=present";
  char *inherited_env[] = {inherited_value, inherited_other, nullptr};
  environ = inherited_env;
  redirects[1] = "/tmp/execution-inherited";
  execute(self, args, std::nullopt, redirects);
  file("/tmp/execution-inherited");
  environ = environment;

  StringRef stream_args[] = {self, "streams"};
  redirects[1] = "/tmp/execution-stdout"; redirects[2] = "/tmp/execution-stderr";
  execute(self, stream_args, std::nullopt, redirects);
  file("/tmp/execution-stdout"); file("/tmp/execution-stderr");
  redirects[1] = "/tmp/execution-merged"; redirects[2] = redirects[1];
  execute(self, stream_args, std::nullopt, redirects);
  file("/tmp/execution-merged");
  redirects[1] = StringRef(""); redirects[2] = StringRef("");
  execute(self, stream_args, std::nullopt, redirects);
  create("/tmp/execution-input", "INPUT!\n");
  StringRef input_args[] = {self, "input"};
  redirects[0] = "/tmp/execution-input"; redirects[1] = "/tmp/execution-copied";
  redirects[2] = std::nullopt;
  execute(self, input_args, std::nullopt, redirects);
  file("/tmp/execution-copied");

  // Child setup failure must not return into this caller or create more children.
  redirects[0] = "/missing/execution-input";
  execute(self, input_args, std::nullopt, redirects);
  redirects[0] = std::nullopt; redirects[1] = "/missing/execution-output";
  execute(self, stream_args, std::nullopt, redirects);
  redirects[1] = "/tmp/execution-unused-stdout"; redirects[2] = "/missing/execution-error";
  execute(self, stream_args, std::nullopt, redirects);
  StringRef missing[] = {"/missing/execution-program"};
  execute(missing[0], missing);
  create("/tmp/execution-invalid", "not a wasm program", 0755);
  StringRef invalid[] = {"/tmp/execution-invalid"};
  execute(invalid[0], invalid);

  int pipefd[2];
  add(pipe(pipefd));
  char descriptor[24]; snprintf(descriptor, sizeof descriptor, "%d", pipefd[0]);
  StringRef held_args[] = {self, "hold", descriptor};
  failed = true; error.clear();
  ProcessInfo held = llvm::sys::ExecuteNoWait(self, held_args, std::nullopt, {},
                                             0, &error, &failed);
  add(held.Pid > 0); add(held.ReturnCode); add(failed); text(error);
  ProcessInfo polled = llvm::sys::Wait(held, 0, &error);
  add(polled.Pid); add(polled.ReturnCode); text(error);
  add(write(pipefd[1], "x", 1)); close(pipefd[0]); close(pipefd[1]);
  ProcessInfo done = llvm::sys::Wait(held, std::nullopt, &error);
  add(done.Pid == held.Pid); add(done.ReturnCode); text(error);
  int status;
  errno = 73; add(waitpid(held.Pid, &status, WNOHANG)); add(errno);

  // Both actual asynchronous execution paths share the existing sleep scheduler.
  StringRef early[] = {self, "sleep", "60000", "17"};
  StringRef later[] = {self, "sleep", "120000", "31"};
  uint64_t start = now();
  ProcessInfo first = llvm::sys::ExecuteNoWait(self, early, std::nullopt);
  ProcessInfo second = llvm::sys::ExecuteNoWait(self, later, std::nullopt);
  add(first.Pid > 0); add(second.Pid > 0); add(first.Pid != second.Pid);
  done = llvm::sys::Wait(second, std::nullopt, &error);
  add(done.Pid == second.Pid); add(done.ReturnCode);
  done = llvm::sys::Wait(first, std::nullopt, &error);
  add(done.Pid == first.Pid); add(done.ReturnCode); add(now() - start);

  StringRef session_args[] = {self, "session"};
  std::optional<StringRef> session_redirects[] = {"/tmp/execution-input", "/tmp/execution-session", std::nullopt};
  ProcessInfo detached = llvm::sys::ExecuteNoWait(self, session_args, std::nullopt,
      session_redirects, 0, &error, &failed, nullptr, true);
  add(detached.Pid > 0); add(failed);
  done = llvm::sys::Wait(detached, std::nullopt, &error);
  add(done.Pid == detached.Pid); add(done.ReturnCode);
  file("/tmp/execution-session");
  add(getsid(0) == original_sid); add(getpid() == original_pid);

  // Actual LLVM empty paths route all three streams through the shared device.
  StringRef null_args[] = {self, "null"};
  std::optional<StringRef> null_redirects[] = {StringRef(""), StringRef(""), StringRef("")};
  execute(self, null_args, std::nullopt, null_redirects);
  file("/tmp/execution-null-child");
  error.clear(); failed = true;
  detached = llvm::sys::ExecuteNoWait(self, null_args, std::nullopt,
      null_redirects, 0, &error, &failed, nullptr, true);
  add(detached.Pid > 0); add(failed); text(error);
  done = llvm::sys::Wait(detached, std::nullopt, &error);
  add(done.Pid == detached.Pid); add(done.ReturnCode); text(error);
  file("/tmp/execution-null-child");
  add(getsid(0) == original_sid); add(getpid() == original_pid);

  // Unsupported options fail before creating a child, even without ErrMsg.
  std::optional<StringRef> side_effect[] = {std::nullopt, "/tmp/execution-unavailable", std::nullopt};
  std::optional<llvm::sys::ProcessStatistics> statistics;
  for (int option = 1; option < 4; ++option) {
    failed = false; error.clear(); errno = 73;
    llvm::BitVector affinity;
    add(llvm::sys::ExecuteAndWait(self, stream_args, std::nullopt, side_effect,
        0, option == 1 ? 1 : 0, &error, &failed,
        option == 2 ? &statistics : nullptr, option == 3 ? &affinity : nullptr));
    add(failed); add(errno); text(error);
    errno = 73; add(waitpid(-1, &status, WNOHANG)); add(errno);
    errno = 73; add(access("/tmp/execution-unavailable", F_OK)); add(errno);
  }
  failed = false; errno = 73;
  detached = llvm::sys::ExecuteNoWait(self, stream_args, std::nullopt,
      side_effect, 1, nullptr, &failed);
  add(detached.Pid); add(failed); add(errno);
  errno = 73; add(waitpid(-1, &status, WNOHANG)); add(errno);
  failed = false; errno = 73;
  detached = llvm::sys::ExecuteNoWait(self, stream_args, std::nullopt,
      llvm::ArrayRef<std::optional<StringRef>>(side_effect, 1), 0, &error, &failed);
  add(detached.Pid); add(failed); add(errno); text(error);
  errno = 73; add(waitpid(-1, &status, WNOHANG)); add(errno);
  add(getpid() == original_pid);
  write(1, Output, Count * sizeof Output[0]);
  return 0;
}
