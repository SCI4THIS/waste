/* Observe real compiler va_list lowering; assertions belong in format.wast. */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>

typedef struct {
  int length;
  int repeated_length;
  int first_argument;
  int saved_errno;
  unsigned char bytes[80];
  unsigned char repeated_bytes[80];
} Observation;

static int observe(int null_destination, size_t capacity, const char *format, ...) {
  Observation result;
  va_list arguments;
  for (size_t i = 0; i < sizeof(result.bytes); i++) {
    result.bytes[i] = 0xa5;
    result.repeated_bytes[i] = 0xa5;
  }
  errno = 17;
  va_start(arguments, format);
  result.length = FORMAT_CALL(null_destination ? 0 : (char *)result.bytes + 8,
                              capacity, format, arguments);
  result.repeated_length = FORMAT_CALL(null_destination ? 0 : (char *)result.repeated_bytes + 8,
                                       capacity, format, arguments);
  result.first_argument = va_arg(arguments, int);
  va_end(arguments);
  result.saved_errno = errno;
  /* Capture both buffers, counts and the original argument cursor unchanged.
   * A short transport write is a fixture failure, not a formatter assertion. */
  return write(1, &result, sizeof(result)) == (ssize_t)sizeof(result) ? 0 : 1;
}

#ifdef __cplusplus
extern "C"
#endif
int main(int argc, char **argv, char **envp) {
  (void)argc; (void)argv; (void)envp;
  int status = 0;
  status |= observe(0, 64, "%d|%s|%lld|%c|%zu|%x|%%", 42, "guest",
                    -4294967297LL, (char)'Q', (size_t)17, 42u);
  status |= observe(0, 7, "%d-%s", 42, "abcdef");
  status |= observe(0, 10, "%d-%s", 42, "abcdef");
  status |= observe(0, 1, "%d-%s", 42, "abcdef");
  status |= observe(0, 0, "%d-%s", 42, "abcdef");
  status |= observe(1, 0, "%d-%s", 42, "abcdef");
  status |= observe(0, 64, "", 42);
  status |= observe(0, 6, "%d:%s", 42, "\xc3\xa9X");
  status |= observe(0, 72, "%d:%" PRIX8 "/%" PRIX8, 42,
                    (uint8_t)0, (uint8_t)UINT8_MAX);
  status |= observe(0, 72, "%d:%" PRIX16 "/%" PRIX16, 42,
                    (uint16_t)0, (uint16_t)UINT16_MAX);
  status |= observe(0, 72, "%d:%" PRIX32 "/%" PRIX32, 42,
                    (uint32_t)0, (uint32_t)UINT32_MAX);
  status |= observe(0, 72, "%d:%" PRIX64 "/%" PRIX64, 42,
                    (uint64_t)0, (uint64_t)UINT64_MAX);
  status |= observe(0, 72, "%d:%" PRIX8 "/%" PRIX16 "/%" PRIX32 "/%" PRIX64
                    "/%d/%" PRIX64 "/%" PRIX32, 42, (uint8_t)0x80,
                    (uint16_t)0x8000, UINT32_C(0x80000000),
                    UINT64_C(0x8000000000000000), 73,
                    UINT64_C(0x0123456789abcdef), UINT32_C(0xdeadbeef));
  status |= observe(0, 72, "%d:%02" PRIX8 "/%04" PRIX16 "/%08" PRIX32
                    "/%016" PRIX64, 42, (uint8_t)0xa, (uint16_t)0xbc,
                    UINT32_C(0xdef), UINT64_C(0x123));
  status |= observe(0, 9, "%d:0x%" PRIX64 ":%d", 42,
                    UINT64_C(0xabcdef0123456789), 73);
  status |= observe(0, 72, "%d:%" PRIx32 "/%" PRIX32 "/%" PRIx64 "/%" PRIX64,
                    42, UINT32_C(0xdeadbeef), UINT32_C(0xdeadbeef),
                    UINT64_C(0xfedcba9876543210), UINT64_C(0xfedcba9876543210));
  status |= observe(0, 72, "%d:%" PRIx8 "/%" PRIx16 "/%" PRIx32 "/%" PRIx64,
                    42, (uint8_t)0xab, (uint16_t)0xcdef,
                    UINT32_C(0x89abcdef), UINT64_C(0x123456789abcdef0));
  status |= observe(0, 72, "%d:%" PRIu8 "/%" PRIu8 "/%" PRIu16 "/%" PRIu16
                    "/%" PRIu32 "/%" PRIu64, 42, (uint8_t)0,
                    (uint8_t)UINT8_MAX, (uint16_t)0, (uint16_t)UINT16_MAX,
                    UINT32_MAX, UINT64_MAX);
  return status;
}
