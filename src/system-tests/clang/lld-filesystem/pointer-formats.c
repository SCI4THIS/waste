// Capture actual C variadic pointer-format calls; expectations are in WAST.
#include <inttypes.h>
#include <stdio.h>

int waste_c_pointer_formats(char *buffer, size_t size) {
  return snprintf(buffer, size,
                  "%" PRIdPTR " %" PRIiPTR " %" PRIuPTR " %" PRIoPTR
                  " %" PRIxPTR " %" PRIXPTR " %lld %u\n",
                  INTPTR_MIN, (intptr_t)-17, UINTPTR_MAX,
                  (uintptr_t)0xfedcba98, (uintptr_t)0xfedcba98,
                  (uintptr_t)0xfedcba98, -4294967298LL, 73u);
}
