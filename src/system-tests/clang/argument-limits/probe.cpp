// Capture observations only; all behavioral expectations live in WAST.
#include "llvm/Config/config.h"
#include "llvm/Support/Program.h"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <waste/process.h>

static_assert(sizeof(waste_exec_limits) == 3 * sizeof(uint32_t));
static_assert(sizeof(decltype(waste_exec_limits::max_bytes)) == 4);
static const char *path = "/tmp/argument-limits.wasm";
static int64_t output[512];
static unsigned count;
static waste_exec_limits limits;
static void record(int64_t value) { output[count++] = value; }
static size_t bytes(char *const *items) {
  size_t result = 0;
  if (items) for (; *items; ++items) result += strlen(*items) + 1;
  return result;
}
static unsigned entries(char *const *items) {
  unsigned result = 0;
  if (items) while (items[result]) ++result;
  return result;
}
static char *string(size_t length) {
  char *result = static_cast<char *>(malloc(length + 1));
  if (!result) _Exit(90);
  memset(result, 'X', length); result[length] = 0;
  return result;
}
static void fit(char **argv, char **env) {
  char **saved = environ; environ = env;
  unsigned size = entries(argv);
  auto *args = new llvm::StringRef[size];
  auto *pointers = new const char *[size];
  for (unsigned i = 0; i < size; ++i) { args[i] = argv[i]; pointers[i] = argv[i]; }
  errno = 73;
  record(llvm::sys::commandLineFitsWithinSystemLimits(
      path, llvm::ArrayRef<llvm::StringRef>(args, size)));
  record(errno);
  record(llvm::sys::commandLineFitsWithinSystemLimits(
      path, llvm::ArrayRef<const char *>(pointers, size)));
  record(errno);
  environ = saved;
  delete[] args; delete[] pointers;
}
static void attempt(char **argv, char **env, const char *image = path) {
  int pipefd[2]; record(pipe(pipefd));
  pid_t pid = fork(); record(pid > 0);
  if (!pid) {
    close(pipefd[0]); dup2(pipefd[1], 3); close(pipefd[1]);
    // A failed exec must leave close-on-exec descriptors and the image intact.
    int guard = open("/tmp/argument-guard", O_CREAT | O_RDWR, 0600);
    fcntl(guard, F_SETFD, FD_CLOEXEC);
    errno = 73;
    int result = execve(image, argv, env);
    int saved_errno = errno;
    char current[256];
    int64_t observations[] = {result, saved_errno, fcntl(guard, F_GETFD),
        waste_get_executable_path(current, sizeof(current)) >= 0 &&
        strcmp(current, path) == 0};
    write(3, observations, sizeof(observations)); _Exit(66);
  }
  close(pipefd[1]);
  int64_t observations[4] = {-99, -99, -99, -99};
  record(read(pipefd[0], observations, sizeof(observations)));
  close(pipefd[0]);
  for (auto value : observations) record(value);
  int status = -1; record(waitpid(pid, &status, 0) == pid); record(status);
}

extern "C" int main(int argc, char **argv, char **envp) {
  if (argc > 1 && strcmp(argv[1], "child") == 0) {
    int64_t observations[] = {argc, entries(envp), bytes(argv) + bytes(envp),
                              argv[argc] == nullptr && envp[entries(envp)] == nullptr};
    write(3, observations, sizeof(observations)); return 42;
  }
  errno = 73; record(waste_get_exec_limits(&limits)); record(errno);
  record(limits.max_args); record(limits.max_env); record(limits.max_bytes);
  record(sysconf(_SC_ARG_MAX)); record(errno);
  record(waste_get_exec_limits(nullptr)); record(errno);
  errno = 73; record(sysconf(-1)); record(errno);
  char **args = static_cast<char **>(calloc(limits.max_args + 2, sizeof(char *)));
  char **env = static_cast<char **>(calloc(limits.max_env + 2, sizeof(char *)));
  if (!args || !env) return 91;
  args[0] = const_cast<char *>(path); args[1] = const_cast<char *>("child");
  // A valid argument beyond the pathname limit; null environment is permitted.
  args[2] = string(1024); fit(args, nullptr); attempt(args, nullptr);
  free(args[2]); args[2] = nullptr;
  // Exact count boundaries, including empty strings as real entries.
  for (unsigned i = 2; i < limits.max_args; ++i) args[i] = const_cast<char *>("");
  fit(args, env); attempt(args, env);
  args[limits.max_args] = const_cast<char *>("");
  fit(args, env); attempt(args, env); args[limits.max_args] = nullptr;
  for (unsigned i = 2; i < limits.max_args; ++i) args[i] = nullptr;
  for (unsigned i = 0; i < limits.max_env; ++i) env[i] = const_cast<char *>("E=");
  fit(args, env); attempt(args, env);
  env[limits.max_env] = const_cast<char *>("E=");
  fit(args, env); attempt(args, env);
  memset(env, 0, (limits.max_env + 2) * sizeof(char *));
  // Combined string budget includes both vectors' NUL terminators.
  env[0] = const_cast<char *>("A=present");
  size_t available = limits.max_bytes - bytes(args) - bytes(env);
  args[2] = string(available - 1);
  fit(args, env); attempt(args, env); free(args[2]);
  args[2] = string(available);
  fit(args, env); attempt(args, env); free(args[2]); args[2] = nullptr;
  // Empty argument strings consume bytes too, including after the budget ends.
  env[0] = string(limits.max_bytes - bytes(args) - 1);
  fit(args, env); attempt(args, env);
  env[1] = const_cast<char *>(""); fit(args, env); attempt(args, env);
  free(env[0]); env[0] = env[1] = nullptr;
  // A changed environment invalidates a previous fit; kernel remains authoritative.
  fit(args, env); env[0] = string(limits.max_bytes);
  attempt(args, env); free(env[0]); env[0] = nullptr;
  // Partial copies followed by a bad string/vector return EFAULT.
  args[2] = reinterpret_cast<char *>(UINTPTR_MAX);
  attempt(args, env); args[2] = nullptr;
  attempt(args, reinterpret_cast<char **>(UINTPTR_MAX));
  // WAT shebang adds interpreter + path, and must recheck count and byte limits.
  for (unsigned i = 2; i < limits.max_args - 1; ++i) args[i] = const_cast<char *>("");
  attempt(args, env, "/tmp/argument-script.wat");
  args[limits.max_args - 1] = const_cast<char *>("");
  attempt(args, env, "/tmp/argument-script.wat");
  for (unsigned i = 2; i < limits.max_args; ++i) args[i] = nullptr;
  args[2] = string(limits.max_bytes - bytes(args) - 1);
  attempt(args, env, "/tmp/argument-script.wat");
  free(args[2]); free(args); free(env);
  size_t size = count * sizeof(output[0]);
  return write(1, output, size) == static_cast<ssize_t>(size) ? 0 : 92;
}
