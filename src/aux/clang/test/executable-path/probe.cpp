// Capture real process-image identity and actual LLVM getMainExecutable calls.
// WAST owns the expected strings, errors, lifetime and fork/exec assertions.
#include "llvm/Support/FileSystem.h"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <sys/wait.h>
#include <unistd.h>
#include <waste/process.h>

struct report {
  int values[32];
  char strings[4][128];
};

static void capture(int phase, char **argv, int waited = 0, int status = 0,
                    int unlinked = -1) {
  report result = {};
  int output = dup(STDOUT_FILENO);
  char path[PATH_MAX], after[PATH_MAX], exact[PATH_MAX + 2];
  result.values[0] = phase;
  result.values[1] = getpid();
  result.values[2] = PATH_MAX;
  errno = 73;
  int length = waste_get_executable_path(path, sizeof(path));
  result.values[3] = length;
  result.values[4] = errno;
  if (length >= 0 && static_cast<size_t>(length) < sizeof(result.strings[0]))
    memcpy(result.strings[0], path, length + 1);
  std::string discovered = llvm::sys::fs::getMainExecutable(nullptr, nullptr);
  if (discovered.size() < sizeof(result.strings[1]))
    memcpy(result.strings[1], discovered.c_str(), discovered.size() + 1);
  result.values[5] = strlen(argv[0]);
  if (strlen(argv[0]) < sizeof(result.strings[3]))
    strcpy(result.strings[3], argv[0]);
  result.values[6] = errno;
  result.values[7] = chdir("/");
  result.values[8] = waste_get_executable_path(after, sizeof(after));
  result.values[9] = length >= 0 && strcmp(path, after) == 0;
  discovered = llvm::sys::fs::getMainExecutable("/fabricated/clang", nullptr);
  if (discovered.size() < sizeof(result.strings[2]))
    memcpy(result.strings[2], discovered.c_str(), discovered.size() + 1);
  memset(exact, '!', sizeof(exact));
  errno = 81;
  result.values[10] = waste_get_executable_path(exact + 1, length + 1);
  result.values[11] = errno;
  result.values[12] = exact[0];
  if (length >= 0 && length < PATH_MAX) {
    result.values[13] = exact[length + 2];
    result.values[14] = exact[length + 1];
  }
  memset(exact, '!', sizeof(exact));
  errno = 0;
  result.values[15] = waste_get_executable_path(exact + 1, length);
  result.values[16] = errno;
  result.values[17] = 1;
  for (char byte : exact)
    result.values[17] &= byte == '!';
  errno = 0;
  result.values[18] = waste_get_executable_path(exact, 0);
  result.values[19] = errno;
  if (phase == 0) {
    result.values[20] = rename("/tmp/executable-path.wasm", "/tmp/executable-path-renamed.wasm");
    result.values[21] = waste_get_executable_path(after, sizeof(after));
    result.values[22] = length >= 0 && strcmp(path, after) == 0;
    result.values[23] = unlink("/tmp/executable-path-renamed.wasm");
    result.values[24] = waste_get_executable_path(after, sizeof(after));
    result.values[25] = length >= 0 && strcmp(path, after) == 0;
  } else if (phase == 1) {
    unsigned char byte = 255;
    result.values[20] = read(unlinked, &byte, 1);
    result.values[21] = byte;
  }
  result.values[26] = waited;
  result.values[27] = status;
  errno = 0;
  result.values[28] = waste_get_executable_path(nullptr, sizeof(path));
  result.values[29] = errno;
  errno = 0;
  result.values[30] = waste_get_executable_path(reinterpret_cast<char *>(UINTPTR_MAX - 7), sizeof(path));
  result.values[31] = errno;
  // Fixed slots do not depend on the pending shared-OFD-offset semantics.
  lseek(output, phase * sizeof(result), SEEK_SET);
  write(output, &result, sizeof(result));
  close(output);
}

extern "C" int main(int argc, char **argv, char **environment) {
  if (argc > 1) {
    argv[0] = const_cast<char *>("spoofed-exec");
    capture(2, argv);
    return 0;
  }
  argv[0] = const_cast<char *>("spoofed-parent");
  int original = open("/tmp/executable-path.wasm", O_RDONLY);
  int alias = dup(original);
  capture(0, argv);
  int child = fork();
  if (child == 0) {
    argv[0] = const_cast<char *>("spoofed-child");
    capture(1, argv, 0, 0, alias);
    char *next[] = {const_cast<char *>("untrusted-argv-zero"), const_cast<char *>("after-exec"), nullptr};
    execve("/tmp/executable-path-copy.wasm", next, environment);
    return 127;
  }
  int status = -1;
  int waited = waitpid(child, &status, 0);
  close(original);
  close(alias);
  capture(3, argv, waited, status);
  return 0;
}
