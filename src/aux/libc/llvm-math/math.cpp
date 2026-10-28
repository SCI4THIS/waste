/* C ABI around the pinned LLVM libc binary64 implementations.
 * The guest uses fixed nearest-even rounding and does not emulate exceptions.
 * LLVM's bounded fast pass avoids unsupported 128-bit compiler-rt imports;
 * this profile does not promise correctly rounded transcendental results.
 */
#include "src/__support/math/exp.h"
#include "src/__support/math/log.h"
#include "src/__support/math/pow.h"
#include "src/__support/math/sin.h"
#include "src/__support/math/cos.h"
#include "src/__support/math/atan2.h"
#include "src/__support/math/fmod.h"
extern "C" double exp(double x) { return LIBC_NAMESPACE::math::exp(x); }
extern "C" double log(double x) { return LIBC_NAMESPACE::math::log(x); }
extern "C" double pow(double x, double y) { return LIBC_NAMESPACE::math::pow(x, y); }
extern "C" double sin(double x) { return LIBC_NAMESPACE::math::sin(x); }
extern "C" double cos(double x) { return LIBC_NAMESPACE::math::cos(x); }
extern "C" double atan2(double x, double y) { return LIBC_NAMESPACE::math::atan2(x, y); }
extern "C" double fmod(double x, double y) { return LIBC_NAMESPACE::math::fmod(x, y); }
