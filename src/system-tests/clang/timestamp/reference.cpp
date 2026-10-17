// Observe the original classic stream formatter on the host, with TZ=UTC.
#include "cases.h"
#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

static void emit(std::uint32_t Value) { (void)write(1, &Value, sizeof(Value)); }
int main() {
  std::vector<std::string> Results;
  for (auto Value : TimestampCases) {
    std::time_t Time = Value;
    std::stringstream Stream;
    Stream.imbue(std::locale("C"));
    Stream << std::put_time(std::gmtime(&Time), "%a %b %e %T %Y");
    Results.push_back(Stream.str());
  }
  Results.emplace_back(); // Missing file leaves the production stream empty.
  std::time_t Time = 0;
  std::stringstream Stream;
  Stream.imbue(std::locale("C"));
  Stream << std::put_time(std::localtime(&Time), "%a %b %e %T %Y");
  Results.push_back(Stream.str());
  emit(Results.size());
  emit(usleep(1000));
  for (const auto &Text : Results) emit(Text.size());
  for (const auto &Text : Results) (void)write(1, Text.data(), Text.size());
}
