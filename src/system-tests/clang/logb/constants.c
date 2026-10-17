/* Observe the public C header, including integer constant-expression use. */
#include <math.h>
#include <unistd.h>

enum { zero_exponent = FP_ILOGB0, nan_exponent = FP_ILOGBNAN };

int logb_c_constants(void) {
  const int values[] = {
    zero_exponent, nan_exponent,
    _Generic(FP_ILOGB0, int: 1, default: 0),
    _Generic(FP_ILOGBNAN, int: 1, default: 0)
  };
  return write(1, values, sizeof(values)) != (ssize_t)sizeof(values);
}
