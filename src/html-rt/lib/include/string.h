#ifndef WASTE_STRING_H
#define WASTE_STRING_H

#include <stddef.h>
#include <stdbool.h>

void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
void *memset_explicit(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
void *memchr(const void *, int, size_t);
void *rawmemchr(const void *, int);
void *mempcpy(void *, const void *, size_t);
size_t strlen(const char *);
size_t strnlen(const char *, size_t);
size_t strspn(const char *, const char *);
size_t strcspn(const char *, const char *);
char *strcpy(char *, const char *);
char *stpcpy(char *, const char *);
char *strncpy(char *, const char *, size_t);
char *strcat(char *, const char *);
char *strncat(char *, const char *, size_t);
int strcmp(const char *, const char *);
int strcoll(const char *, const char *);
bool str_endswith(const char *, const char *);
size_t mbslen(const char *);
int strncmp(const char *, const char *, size_t);
int mbscasecmp(const char *, const char *);
char *strchr(const char *, int);
char *strnul(const char *);
char *strrchr(const char *, int);
char *strstr(const char *, const char *);
char *strdup(const char *);
const char *strerror(int);

static inline int memeq(const void *a, const void *b, size_t n) {
  return memcmp(a, b, n) == 0;
}
static inline int streq(const char *a, const char *b) {
  return strcmp(a, b) == 0;
}

#endif
