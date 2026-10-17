/* Capture compiler-generated variadic calls; all expectations live in WAST. */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

typedef struct {
  int length, repeated_length, saved_errno, repeated_errno, first_argument;
  wchar_t text[128], repeated[128];
} observation;

static void initialize(observation *result) {
  memset(result, 0, sizeof(*result));
  for (size_t at = 0; at < 128; at++) result->text[at] = result->repeated[at] = 0x1234abcd;
}
static int capture(const observation *result) {
  return write(1, result, sizeof(*result)) == (ssize_t)sizeof(*result) ? 0 : 1;
}
static int observe(size_t capacity, const wchar_t *format, ...) {
  observation result;
  initialize(&result);
  va_list arguments;
  va_start(arguments, format);
  errno = 17;
  result.length = FORMAT_CALL(result.text + 2, capacity, format, arguments);
  result.saved_errno = errno;
  errno = 17;
  result.repeated_length = FORMAT_CALL(result.repeated + 2, capacity, format, arguments);
  result.repeated_errno = errno;
  result.first_argument = va_arg(arguments, int);
  va_end(arguments);
  return capture(&result);
}

#ifdef __cplusplus
extern "C"
#endif
int main(int argc, char **argv, char **envp) {
  (void)argc; (void)argv; (void)envp;
  int status = 0;
  status |= observe(118, L"%d|%.2f|%.35Lf|%lld", 42, 1.25,
                    0x1.0000000000000000000000000001p0L, -4294967297LL);
  status |= observe(118, L"%d|%ls|%lc|%.2s", 42, L"\u00e9\U0001f600",
                    (wint_t)0x1f600, "\xc3\xa9\xf0\x9f\x98\x80X");
  status |= observe(118, L"%d|%.6Le|%La", 42, 0x1p-16494L,
                    0x1.0000000000000000000000000001p0L);
  status |= observe(7, L"%d-%ls", 42, L"abcdef");
  status |= observe(10, L"%d-%ls", 42, L"abcdef");
  status |= observe(1, L"%d-%ls", 42, L"abcdef");
  status |= observe(0, L"%d-%ls", 42, L"abcdef");
  status |= observe(118, L"%d|%*.*Lf|%ju|%td", 42, -10, 3, 0.125L,
                    (uintmax_t)18446744073709551615ULL, (ptrdiff_t)-7);
  observation result;
  initialize(&result);
  errno = 17;
  result.length = DIRECT_CALL(result.text + 2, 118, L"%.35Lf|%lc",
                             0x1.0000000000000000000000000001p0L, (wint_t)0x1f600);
  result.saved_errno = errno;
  result.repeated_length = result.length; result.repeated_errno = errno;
  result.first_argument = 42;
  memcpy(result.repeated, result.text, sizeof(result.text));
  status |= capture(&result);
  return status;
}
