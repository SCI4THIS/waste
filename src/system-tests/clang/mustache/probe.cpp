// Capture numeric interpolation and string ownership; WAST owns assertions.
#include <cstdint>
#include <cstring>
#include <string>
#include <unistd.h>
#ifdef MUSTACHE_HOST_REFERENCE
#include <locale>
#include <sstream>
#else
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/WasteMustache.h"
#include <cerrno>
#endif

static constexpr uint64_t Inputs[] = {
#include "values.inc"
};
struct Observation { uint32_t Length; char Text[32]; };
static_assert(sizeof(Observation) == 36 && sizeof(double) == sizeof(uint64_t));
#ifdef MUSTACHE_HOST_REFERENCE
int main() {
#else
extern "C" int main(int argc, char **argv, char **environment) {
  (void)argc; (void)argv; (void)environment;
  errno = EDOM;
#endif
  Observation Output[sizeof(Inputs) / sizeof(Inputs[0])] = {};
  std::string Combined;
  unsigned Index = 0;
  for (uint64_t Bits : Inputs) {
    double Number;
    std::memcpy(&Number, &Bits, sizeof(Number));
#ifdef MUSTACHE_HOST_REFERENCE
    std::ostringstream Stream;
    Stream.imbue(std::locale::classic());
    Stream << Number;
    auto Text = Stream.str();
#else
    auto Text = llvm::detail::wasteMustacheNumber(Number);
#endif
    Output[Index].Length = Text.size();
    size_t Copied = Text.size() + 1;
    if (Copied > sizeof(Output[Index].Text)) Copied = sizeof(Output[Index].Text);
    std::memcpy(Output[Index++].Text, Text.c_str(), Copied);
    Combined.append(Text);
    Combined.push_back('|');
  }
  if (write(1, Output, sizeof(Output)) != static_cast<ssize_t>(sizeof(Output))) return 1;
#ifndef MUSTACHE_HOST_REFERENCE
  uint32_t Metadata[17] = {};
  unsigned Count = 0;
  auto record = [&](unsigned Value) { Metadata[Count++] = Value; };
  record(LLVM_WASTE_GUEST);
  record(_LIBCPP_HAS_LOCALIZATION);
  record(_LIBCPP_HAS_WIDE_CHARACTERS);
  record(LLVM_ENABLE_THREADS);
  record(Index);
  record(errno);
  record(Combined.size());
  record(Combined.size() > sizeof(Observation::Text));
  auto Copy = Combined;
  record(Copy == Combined);
  record(Copy.data() != Combined.data());
  auto *Pointer = Copy.data();
  auto Moved = std::move(Copy);
  record(Moved.data() == Pointer);
  record(Moved == Combined);
  Combined[0] = '!';
  record(static_cast<unsigned char>(Combined[0]));
  record(static_cast<unsigned char>(Moved[0]));
  record(Moved.size());
  Combined.clear();
  record(Combined.size());
  auto Tail = Moved.substr(Moved.size() - 5);
  record(Tail.size());
  if (write(1, Metadata, sizeof(Metadata)) != static_cast<ssize_t>(sizeof(Metadata))) return 1;
  char Text[16] = {};
  std::memcpy(Text, Tail.c_str(), Tail.size() + 1);
  if (write(1, Text, sizeof(Text)) != static_cast<ssize_t>(sizeof(Text))) return 1;
#endif
  return 0;
}
