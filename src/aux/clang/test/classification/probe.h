/* Capture actual compiler-generated C/C++ observations; assertions are WAST. */
#include <errno.h>
#include <float.h>
#include <unistd.h>

struct observation {
  int classification, finite, normal, nan, infinite, negative;
  int calls[6];
  int saved_errno;
};

static volatile int calls;

/* Volatile input plus a counted function call prevents constant folding and
   exposes repeated argument evaluation in macros or C++ wrappers. */
#define OBSERVER(TYPE, NAME) \
  static volatile TYPE NAME##_input; \
  static TYPE NAME##_next(void) { calls++; return NAME##_input; } \
  static int NAME##_observe(TYPE value) { \
    struct observation output; \
    NAME##_input = value; \
    errno = 17; \
    calls = 0; output.classification = CLASSIFY(NAME##_next()); output.calls[0] = calls; \
    calls = 0; output.finite = !!FINITE(NAME##_next()); output.calls[1] = calls; \
    calls = 0; output.normal = !!NORMAL(NAME##_next()); output.calls[2] = calls; \
    calls = 0; output.nan = !!NAN_TEST(NAME##_next()); output.calls[3] = calls; \
    calls = 0; output.infinite = !!INF_TEST(NAME##_next()); output.calls[4] = calls; \
    calls = 0; output.negative = !!SIGN_TEST(NAME##_next()); output.calls[5] = calls; \
    output.saved_errno = errno; \
    return write(1, &output, sizeof(output)) != (ssize_t)sizeof(output); \
  }
OBSERVER(float, single)
OBSERVER(double, double_precision)
OBSERVER(long double, quad)
#undef OBSERVER

#ifdef __cplusplus
extern "C"
#endif
int main(int argc, char **argv, char **environment) {
  (void)argc; (void)argv; (void)environment;
  const int metadata[] = {
    FP_NAN, FP_INFINITE, FP_ZERO, FP_SUBNORMAL, FP_NORMAL,
    sizeof(float), sizeof(double), sizeof(long double),
    FLT_MANT_DIG, DBL_MANT_DIG, LDBL_MANT_DIG
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
  int status = write(1, metadata, sizeof(metadata)) != (ssize_t)sizeof(metadata);
  for (unsigned index = 0; index < sizeof(singles) / sizeof(*singles); index++)
    status |= single_observe(singles[index]);
  for (unsigned index = 0; index < sizeof(doubles) / sizeof(*doubles); index++)
    status |= double_precision_observe(doubles[index]);
  for (unsigned index = 0; index < sizeof(quads) / sizeof(*quads); index++)
    status |= quad_observe(quads[index]);
  return status;
}
