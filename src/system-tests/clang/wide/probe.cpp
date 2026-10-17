// Capture real libc++ header/guest-provider observations; WAST asserts results.
#include <cwchar>
#include <cwctype>
#include <cstdint>
#include <cinttypes>
#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <unistd.h>

static_assert(std::is_same_v<decltype(std::wcschr((const wchar_t *)nullptr, 0)), const wchar_t *>);
static_assert(std::is_same_v<decltype(std::wcschr((wchar_t *)nullptr, 0)), wchar_t *>);
static_assert(std::is_same_v<decltype(std::wcsstr((const wchar_t *)nullptr, L"")), const wchar_t *>);
static_assert(std::is_same_v<decltype(std::towlower(0)), wint_t>);
static_assert(WCHAR_MIN == (-2147483647 - 1) && WCHAR_MAX == 2147483647);
static_assert(std::is_unsigned_v<wint_t> && WINT_MIN == 0 && WINT_MAX == UINT32_MAX);

static int sign(int value) { return (value > 0) - (value < 0); }

struct conversion_record {
  uint64_t value;
  uint32_t end;
  uint32_t error;
};

struct floating_record {
  uint64_t low, high;
  uint32_t end, error;
};

struct wide_report {
  uint32_t fields[32];
  conversion_record conversions[6];
  floating_record floating[3];
};

template <typename Convert>
static conversion_record capture(Convert convert, const wchar_t *text, int base) {
  errno = 17;
  wchar_t *end = nullptr;
  auto value = convert(text, &end, base);
  return {static_cast<uint64_t>(value), static_cast<uint32_t>(end - text),
          static_cast<uint32_t>(errno)};
}

template <typename Convert>
static floating_record capture_float(Convert convert, const wchar_t *text) {
  errno = 17;
  wchar_t *end = nullptr;
  auto value = convert(text, &end);
  floating_record record = {};
  std::memcpy(&record.low, &value, sizeof(value));
  record.end = static_cast<uint32_t>(end - text);
  record.error = static_cast<uint32_t>(errno);
  return record;
}

extern "C" int main(int argc, char **argv, char **environment) {
  (void)argc; (void)argv; (void)environment;
  // No owning string construction: the complete runtime archive/startup
  // checks remain pending until its wide format provider exists.
  std::wstring_view view(L"A\u00e9\U0001f600-compiler");
  volatile wchar_t searched = 0x1f600;
  wchar_t source[] = {65, 0, 0x1f600, -1, WCHAR_MIN, WCHAR_MAX};
  wchar_t target[] = {0x12345678, 0, 0, 0, 0, 0, 0, 0x76543210};
  wide_report output = {};
  auto &report = output.fields;
  report[0] = sizeof(wchar_t);
  report[1] = std::is_signed_v<wchar_t>;
  report[2] = view.size();
  report[3] = view.find(searched);
  report[4] = view.find(L"compiler");
  report[5] = view.find(L'Z');
  report[6] = sign(view.substr(4).compare(L"compiler"));
  report[7] = sign(view.compare(L"B"));
  report[8] = std::char_traits<wchar_t>::copy(target + 1, source, 6) == target + 1;
  report[9] = sign(std::wmemcmp(target + 1, source, 6));
  report[10] = std::char_traits<wchar_t>::move(target + 2, target + 1, 5) == target + 2;
  report[11] = target[3];
  report[12] = target[4];
  report[13] = target[5];
  report[14] = target[6];
  report[15] = std::char_traits<wchar_t>::assign(target + 2, 3, 0x1234abcd) == target + 2;
  report[16] = target[2];
  report[17] = target[3];
  report[18] = target[4];
  report[19] = target[0];
  report[20] = target[7];
  report[21] = std::wmemset(target + 1, -1, 1) == target + 1;
  report[22] = target[1];
  report[23] = std::wmemcpy(target + 1, source, 1) == target + 1;
  report[24] = target[1];
  report[25] = std::wmemmove(target + 2, source + 4, 1) == target + 2;
  report[26] = target[2];
  report[27] = std::char_traits<wchar_t>::find(source, 6, 0x1f600) - source;
  report[28] = std::char_traits<wchar_t>::length(source);
  report[29] = sign(std::char_traits<wchar_t>::compare(source + 4, source + 5, 1));
  // Use one array, rather than assuming that string literals are merged.
  const wchar_t search[] = L"ababa";
  report[30] = std::wcsrchr(search, L'a') - search;
  report[31] = std::wcsstr(search, L"ba") - search;
  output.conversions[0] = capture(std::wcstol, L" \t-2147483648!", 10);
  output.conversions[1] = capture(std::wcstoul, L"4294967296!", 10);
  output.conversions[2] = capture(std::wcstoll, L"-0x8000000000000000!", 0);
  output.conversions[3] = capture(std::wcstoull, L"-1!", 10);
  output.conversions[4] = capture(std::wcstoimax, L"+9223372036854775808!", 10);
  output.conversions[5] = capture(std::wcstoumax, L"0xffffffffffffffff!", 0);
  output.floating[0] = capture_float(std::wcstof,
    L"1.0000000596046447753906250000000000000000001!");
  output.floating[1] = capture_float(std::wcstod, L"0x1p-1074!");
  output.floating[2] = capture_float(std::wcstold, L"0x1.0000000000000000000000000001p0!");
  return write(1, &output, sizeof(output)) != (ssize_t)sizeof(output);
}
