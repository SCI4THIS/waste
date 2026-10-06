#ifndef WASTE_STDIO_EXT_H
#define WASTE_STDIO_EXT_H

#include <waste/abi/availability.h>

#include <stdio.h>

size_t __freadahead(FILE *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int __freading(FILE *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int __fwriting(FILE *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
void __fseterr(FILE *);

#endif
