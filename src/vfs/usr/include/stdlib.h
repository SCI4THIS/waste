#ifndef WASTE_STDLIB_H
#define WASTE_STDLIB_H

#include <waste/abi/availability.h>

#include <stddef.h>

typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;

#ifdef __cplusplus
extern "C" {
#endif

#define MB_CUR_MAX 1

void *malloc(size_t);
void *aligned_alloc(size_t, size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
int posix_memalign(void **, size_t, size_t);
void *reallocarray(void *, size_t, size_t);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
void *bsearch(const void *, const void *, size_t, size_t,
              int (*)(const void *, const void *));
void qsort_r(void *, size_t, size_t,
             int (*)(const void *, const void *, void *), void *) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
void free(void *);
_Noreturn void abort(void);
_Noreturn void exit(int);
_Noreturn void _Exit(int);
int atexit(void (*)(void));
int atoi(const char *);
long atol(const char *);
long strtol(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
long long strtoll(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
double strtod(const char *, char **);
float strtof(const char *, char **);
long double strtold(const char *, char **);
int abs(int);
long labs(long);
long long llabs(long long);
div_t div(int, int);
ldiv_t ldiv(long, long);
lldiv_t lldiv(long long, long long);
int rand(void);
void srand(unsigned int);
char *getenv(const char *);
char *secure_getenv(const char *);
extern char **environ;
int setenv(const char *, const char *, int);
int unsetenv(const char *);
int putenv(char *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
char *canonicalize_file_name(const char *) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
char *realpath(const char *, char *);
int mkstemp(char *);
int mkostemp(char *, int);

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#ifdef __cplusplus
}
#endif

#endif
