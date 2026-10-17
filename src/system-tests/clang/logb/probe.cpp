/* Use the same LLVM libc helper configuration as Support/APFloat.cpp. */
#define LIBC_NAMESPACE __llvm_libc_apfloat
#define LIBC_MATH (LIBC_MATH_NO_ERRNO | LIBC_MATH_NO_EXCEPT)
#include "src/__support/FPUtil/ManipulationFunctions.h"
#include <cmath>
#include <errno.h>
#include <type_traits>
#include <unistd.h>

extern "C" int logb_c_constants(void);

template <typename T> static int observe(T value) {
  // Read twice at runtime; do not replace the upstream helper or narrow input.
  volatile T input = value;
  errno = 17;
  int integer = LIBC_NAMESPACE::fputil::intlogb<int>(input);
  long wide_integer = LIBC_NAMESPACE::fputil::intlogb<long>(input);
  const int output[] = {integer, static_cast<int>(wide_integer), errno};
  return write(1, output, sizeof(output)) != (ssize_t)sizeof(output);
}

extern "C" int main(int argc, char **argv, char **environment) {
  (void)argc; (void)argv; (void)environment;
  // C++ constant expressions and the real helper's int/long specializations.
  enum { zero_exponent = FP_ILOGB0, nan_exponent = FP_ILOGBNAN };
  using LIBC_NAMESPACE::fputil::IntLogbConstants;
  const int metadata[] = {
    zero_exponent, nan_exponent,
    std::is_same<decltype(FP_ILOGB0), int>::value,
    std::is_same<decltype(FP_ILOGBNAN), int>::value,
    IntLogbConstants<int>::FP_LOGB0, IntLogbConstants<int>::FP_LOGBNAN,
    static_cast<int>(IntLogbConstants<long>::FP_LOGB0),
    static_cast<int>(IntLogbConstants<long>::FP_LOGBNAN)
  };
  const float singles[] = {
    0.0f, -0.0f, 0x1p-149f, -0x1p-149f, 0x0.fffffep-126f,
    0x1p-126f, 0x1.fffffep+127f, 1.5f, -1.5f,
    INFINITY, -INFINITY, NAN, -NAN
  };
  const double doubles[] = {
    0.0, -0.0, 0x1p-1074, -0x1p-1074, 0x0.fffffffffffffp-1022,
    0x1p-1022, 0x1.fffffffffffffp+1023, 1.5, -1.5,
    HUGE_VAL, -HUGE_VAL, __builtin_nan(""), -__builtin_nan("")
  };
  const long double quads[] = {
    0.0L, -0.0L, 0x1p-16494L, -0x1p-16494L,
    0x0.ffffffffffffffffffffffffffffp-16382L,
    0x1p-16382L, 0x1.ffffffffffffffffffffffffffffp+16383L, 1.5L, -1.5L,
    HUGE_VALL, -HUGE_VALL, __builtin_nanl(""), -__builtin_nanl("")
  };
  int status = logb_c_constants();
  status |= write(1, metadata, sizeof(metadata)) != (ssize_t)sizeof(metadata);
  for (float value : singles) status |= observe(value);
  for (double value : doubles) status |= observe(value);
  for (long double value : quads) status |= observe(value);
  return status;
}
