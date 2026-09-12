#ifndef FREESTANDING_STDLIB_H
#define FREESTANDING_STDLIB_H

#include <stddef.h>
#include <stdint.h>

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif
#ifndef UINT32_MAX
#define UINT32_MAX ((uint32_t)-1)
#endif

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void  free(void *p);

double strtod(const char *s, char **endptr);
float  strtof(const char *s, char **endptr);
long   strtol(const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);
unsigned long long strtoull(const char *s, char **endptr, int base);

int    abs(int x);
void   exit(int status);
char  *getenv(const char *name);

#endif
