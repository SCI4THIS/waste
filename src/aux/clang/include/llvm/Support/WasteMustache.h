// Guest numeric interpolation for LLVM's disabled-localization profile.
// Keep the classic stream's default precision without requiring C++ locales.
#ifndef LLVM_SUPPORT_WASTEMUSTACHE_H
#define LLVM_SUPPORT_WASTEMUSTACHE_H

#include <cstdlib>
#include <cwchar>
#include <string>

namespace llvm::detail {

inline std::string wasteMustacheNumber(double Number) {
  // Six significant digits, sign/decimal point and a signed three-digit
  // binary64 exponent fit here. The shared wide formatter uses exact integer
  // digits and nearest/ties-to-even rounding; narrow printf lacks floats.
  constexpr unsigned Capacity = 32;
  wchar_t Wide[Capacity];
  int Count = std::swprintf(Wide, Capacity, L"%.6g", Number);
  if (Count < 0 || static_cast<unsigned>(Count) >= Capacity)
    std::abort();
  char Narrow[Capacity];
  for (int I = 0; I != Count; ++I) {
    if (static_cast<unsigned>(Wide[I]) > 127)
      std::abort();
    Narrow[I] = static_cast<char>(Wide[I]);
  }
  return std::string(Narrow, static_cast<unsigned>(Count));
}

} // namespace llvm::detail

#endif
