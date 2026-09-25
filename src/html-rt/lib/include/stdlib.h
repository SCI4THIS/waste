#ifndef WASTE_STDLIB_H
#define WASTE_STDLIB_H

#include <stddef.h>

#define MB_CUR_MAX 1

void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
int posix_memalign(void **, size_t, size_t);
void *reallocarray(void *, size_t, size_t);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
void qsort_r(void *, size_t, size_t,
             int (*)(const void *, const void *, void *), void *);
void free(void *);
_Noreturn void abort(void);
_Noreturn void exit(int);
int atexit(void (*)(void));
int atoi(const char *);
long strtol(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
double strtod(const char *, char **);
float strtof(const char *, char **);
long double strtold(const char *, char **);
int abs(int);
char *getenv(const char *);
char *secure_getenv(const char *);
extern char **environ;
int setenv(const char *, const char *, int);
int unsetenv(const char *);
int putenv(char *);
char *canonicalize_file_name(const char *);
int mkstemp(char *);
int mkostemp(char *, int);

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#endif
