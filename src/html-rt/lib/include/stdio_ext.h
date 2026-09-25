#ifndef WASTE_STDIO_EXT_H
#define WASTE_STDIO_EXT_H

#include <stdio.h>

size_t __freadahead(FILE *);
int __freading(FILE *);
int __fwriting(FILE *);
void __fseterr(FILE *);

#endif
