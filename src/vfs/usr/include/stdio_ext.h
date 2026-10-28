#ifndef WASTE_STDIO_EXT_H
#define WASTE_STDIO_EXT_H

#include <waste/abi/availability.h>

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t __fpending(FILE *);
size_t __freadahead(FILE *);
int __freading(FILE *);
int __fwriting(FILE *);
void __fseterr(FILE *);

#ifdef __cplusplus
}
#endif

#endif
