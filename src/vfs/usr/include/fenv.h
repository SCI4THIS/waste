#ifndef WASTE_FENV_H
#define WASTE_FENV_H

/* Wasm has fixed round-to-nearest arithmetic and no sticky exception flags.
 * Only supported modes/exceptions are advertised; no host fenv is exposed. */
#define FE_TONEAREST 0
#define FE_ALL_EXCEPT 0
typedef unsigned int fexcept_t;
typedef struct { unsigned int __rounding_mode; } fenv_t;
#define FE_DFL_ENV ((const fenv_t *)-1)

#ifdef __cplusplus
extern "C" {
#endif

int feclearexcept(int);
int fegetexceptflag(fexcept_t *, int);
int feraiseexcept(int);
int fesetexceptflag(const fexcept_t *, int);
int fetestexcept(int);
int fegetround(void);
int fesetround(int);
int fegetenv(fenv_t *);
int feholdexcept(fenv_t *);
int fesetenv(const fenv_t *);
int feupdateenv(const fenv_t *);

#ifdef __cplusplus
}
#endif
#endif
