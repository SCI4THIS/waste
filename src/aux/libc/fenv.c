#include <fenv.h>

/* No process-global or host floating-point state: the Wasm environment is
 * immutable. Reject unsupported requests instead of reporting a mode/flag
 * change that guest arithmetic cannot implement. None of these APIs set errno. */
int fegetround(void) { return FE_TONEAREST; }
int fesetround(int mode) { return mode == FE_TONEAREST ? 0 : -1; }
int feclearexcept(int excepts) { return excepts == FE_ALL_EXCEPT ? 0 : -1; }
int feraiseexcept(int excepts) { return excepts == FE_ALL_EXCEPT ? 0 : -1; }
int fetestexcept(int excepts) { return excepts == FE_ALL_EXCEPT ? 0 : -1; }

int fegetexceptflag(fexcept_t *flag, int excepts) {
    if (!flag || excepts != FE_ALL_EXCEPT) return -1;
    *flag = 0;
    return 0;
}

int fesetexceptflag(const fexcept_t *flag, int excepts) {
    if (!flag || excepts != FE_ALL_EXCEPT || *flag != 0) return -1;
    return 0;
}

int fegetenv(fenv_t *env) {
    if (!env) return -1;
    env->__rounding_mode = FE_TONEAREST;
    return 0;
}

int feholdexcept(fenv_t *env) { return fegetenv(env); }

int fesetenv(const fenv_t *env) {
    if (env == FE_DFL_ENV) return 0;
    if (!env || env->__rounding_mode != FE_TONEAREST) return -1;
    return 0;
}

int feupdateenv(const fenv_t *env) { return fesetenv(env); }
