#include "clang/Lex/WasteTimestamp.h"
#include "clang/Lex/WastePragma.h"
#include "cases.h"
#include <cstdint>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

static void emit(std::uint32_t Value) { (void)write(1, &Value, sizeof(Value)); }
extern "C" int main(int, char **) {
  std::vector<std::string> Results;
  for (auto Value : TimestampCases) {
    std::time_t Time = Value;
    Results.push_back(clang::detail::wasteTimestamp(std::gmtime(&Time)));
  }
  Results.push_back(clang::detail::wasteTimestamp(nullptr));
  std::time_t Time = 0;
  Results.push_back(clang::detail::wasteTimestamp(std::localtime(&Time)));
  // Retained strings outlive libc's reused calendar buffer and a shared yield.
  int Slept = usleep(1000);
  auto Start = std::chrono::steady_clock::now();
  clang::detail::wastePragmaSleep();
  std::int64_t Elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - Start).count();
  int ClockFile = open("/tmp/pragma-sleep.bin", O_WRONLY | O_CREAT | O_TRUNC, 0600);
  (void)write(ClockFile, &Elapsed, sizeof(Elapsed));
  (void)close(ClockFile);
  emit(Results.size());
  emit(Slept);
  for (const auto &Text : Results) emit(Text.size());
  for (const auto &Text : Results) (void)write(1, Text.data(), Text.size());
  return 37;
}
