// C-locale __TIMESTAMP__ formatting for the guest's localization-free C++ ABI.
#ifndef WASTE_CLANG_LEX_TIMESTAMP_H
#define WASTE_CLANG_LEX_TIMESTAMP_H

#include <ctime>
#include <string>

namespace clang::detail {
inline std::string wasteTimestamp(const std::tm *Time) {
  if (!Time)
    return {};
  // Reuse shared libc's calendar/weekday/month formatting. Spell out %T using
  // its component directives. %Y in the classic C locale has no minimum width;
  // append the full signed year without narrowing or a four-digit truncation.
  char Prefix[32];
  std::size_t Length = std::strftime(Prefix, sizeof(Prefix),
                                   "%a %b %e %H:%M:%S ", Time);
  if (!Length)
    return {};
  return std::string(Prefix, Length) +
         std::to_string(static_cast<long long>(Time->tm_year) + 1900);
}
} // namespace clang::detail
#endif
