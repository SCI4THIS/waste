/*
 * errno.c — Global errno and yydebug definitions.
 *
 * Shared by both native and Wasm builds.
 */

#include <stddef.h>
#include <stdint.h>

int errno = 0;
__attribute__((weak)) int yydebug = 0;
