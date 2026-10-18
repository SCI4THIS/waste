// Observe actual LLVM Wait; all runtime expectations belong to WAST.
#include "llvm/Config/config.h"
#include "llvm/Support/Program.h"
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

using llvm::sys::ProcessInfo;
using llvm::sys::ProcessStatistics;
using llvm::sys::Wait;
static int64_t output[512];
static unsigned count;
static volatile sig_atomic_t caught;
static void handler(int) { ++caught; }
static void record(int64_t value) { output[count++] = value; }
static void text(const std::string &value) {
  int64_t words[8] = {};
  record(value.size());
  memcpy(words, value.data(), value.size() < sizeof(words) ? value.size() : sizeof(words));
  for (auto word : words) record(word);
}
static ProcessInfo process(pid_t pid) {
  ProcessInfo result;
  result.Pid = pid;
  result.Process = pid;
  return result;
}
static void live(pid_t pid) {
  errno = 0; record(kill(pid, 0)); record(errno);
}
static void seed(std::optional<ProcessStatistics> &stats) {
  using namespace std::chrono;
  stats = ProcessStatistics{microseconds(123), microseconds(77), 999};
}

extern "C" int main(int, char **, char **) {
  record(LLVM_WASTE_GUEST); record(LLVM_ENABLE_THREADS);
  const int codes[] = {0, 37, 255, 126, 127};
  for (auto code : codes) {
    int ready[2]; record(pipe(ready));
    pid_t pid = fork();
    if (!pid) { close(ready[0]); write(ready[1], "Z", 1); _Exit(code); }
    record(pid > 0); close(ready[1]);
    char byte; record(read(ready[0], &byte, 1)); record(byte);
    record(read(ready[0], &byte, 1)); record(close(ready[0]));
    std::string error;
    errno = 73;
    auto result = Wait(process(pid), std::nullopt, &error);
    record(result.Pid == pid); record(result.ReturnCode); record(errno);
    text(error); live(pid);
  }

  // Poll a blocked child without disturbing the existing SIGALRM handler.
  struct sigaction action = {}, old_alarm = {}, observed = {};
  action.sa_handler = handler; sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  record(sigaction(SIGALRM, &action, &old_alarm));
  int gate[2]; record(pipe(gate));
  pid_t blocked = fork();
  if (!blocked) { close(gate[1]); char byte; read(gate[0], &byte, 1); _Exit(39); }
  record(blocked > 0); close(gate[0]);
  std::string error;
  errno = 73;
  auto result = Wait(process(blocked), 0, &error, nullptr, true);
  record(result.Pid); record(result.ReturnCode); record(errno); text(error);
  record(sigaction(SIGALRM, nullptr, &observed));
  record(observed.sa_handler == handler); record(observed.sa_flags);
  live(blocked);

  std::optional<ProcessStatistics> stats;
  for (unsigned mode = 0; mode < 3; ++mode) {
    seed(stats); error.clear(); errno = 73;
    result = Wait(process(blocked), mode == 2 ? std::optional<unsigned>(1) : std::nullopt,
                  mode == 1 ? nullptr : &error, &stats);
    record(result.Pid); record(result.ReturnCode); record(errno);
    record(stats.has_value()); text(error); live(blocked);
  }
  record(sigaction(SIGALRM, nullptr, &observed));
  record(observed.sa_handler == handler); record(observed.sa_flags);
  record(sigaction(SIGALRM, &old_alarm, nullptr));
  record(write(gate[1], "R", 1)); record(close(gate[1]));
  error.clear(); errno = 73;
  result = Wait(process(blocked), std::nullopt, &error);
  record(result.Pid == blocked); record(result.ReturnCode); record(errno); text(error);
  error.clear(); errno = 0;
  result = Wait(process(blocked), 0, &error);
  record(result.Pid); record(result.ReturnCode); record(errno); text(error);

  // Preserve LLVM's unhandled-signal result and error message.
  record(pipe(gate));
  pid_t killed = fork();
  if (!killed) { close(gate[1]); char byte; read(gate[0], &byte, 1); _Exit(99); }
  record(killed > 0); close(gate[0]);
  record(kill(killed, SIGTERM));
  error.clear(); errno = 73;
  result = Wait(process(killed), std::nullopt, &error);
  record(result.Pid == killed); record(result.ReturnCode); record(errno); text(error);
  record(close(gate[1])); live(killed);

  // Indefinite LLVM Wait retries an interrupted shared waitpid call.
  for (unsigned restart = 0; restart < 2; ++restart) {
    caught = 0;
    struct sigaction old = {};
    action.sa_flags = restart ? SA_RESTART : 0;
    record(sigaction(SIGUSR1, &action, &old));
    pid_t parent = getpid(), sender = fork();
    if (!sender) { usleep(20000); kill(parent, SIGUSR1); usleep(20000); _Exit(41); }
    record(sender > 0); error.clear(); errno = 73;
    result = Wait(process(sender), std::nullopt, &error);
    record(result.Pid == sender); record(result.ReturnCode); record(errno);
    record(caught); text(error);
    record(sigaction(SIGUSR1, &old, nullptr)); live(sender);
  }
  record(getpid() > 0);
  return write(1, output, count * sizeof(output[0])) !=
         static_cast<ssize_t>(count * sizeof(output[0]));
}
