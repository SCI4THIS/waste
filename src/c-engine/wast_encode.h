#ifndef WAST_ENCODE_H
#define WAST_ENCODE_H

#include "wast_types.h"
#include <stddef.h>

/*
 * Encode a wast_module to binary WebAssembly.
 * Returns an allocated buffer (caller must free) with *size set to its length.
 * Returns NULL on error and sets error[0..255].
 */
uint8_t *wast_encode_module(const wast_module *module, size_t *size, char *error);

#endif /* WAST_ENCODE_H */
