#ifndef WASTE_BASH_STRING_H
#define WASTE_BASH_STRING_H
#include_next <string.h>
/* The selected Bash fallback defines bcopy with this signature. */
void bcopy(void *, void *, size_t);
char *strcasestr(const char *, const char *);
#endif
