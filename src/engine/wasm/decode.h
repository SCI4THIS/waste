#ifndef WASTE_WASM_DECODE_H
#define WASTE_WASM_DECODE_H

#include "module.h"
#include "reader.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    WASM_DECODE_OK = 0,
    WASM_DECODE_FORMAT,
    WASM_DECODE_OUT_OF_MEMORY
} wasm_decode_status;

typedef struct {
    wasm_decode_status status;
    size_t offset;
    char message[128];
} wasm_decode_error;

/* Decode an independently owned, uninstantiated module. The source buffer may
 * be released as soon as this succeeds, and the result may be instantiated
 * repeatedly until disposed. */
wasm_decode_status wasm_decode_module(const uint8_t *bytes, size_t size,
                                      wasm_module *module,
                                      wasm_decode_error *error);

/* Decode the module's import declarations into owned storage. Other sections
 * remain in the borrowed source buffer. Retained for compatibility with the
 * Stage 2 loader; new code should use wasm_decode_module. */
wasm_decode_status wasm_decode_module_imports(const uint8_t *bytes, size_t size,
                                               wasm_module *module,
                                               wasm_decode_error *error);

/* UTF-8 validator shared across subsystems. */
int valid_utf8(const uint8_t *bytes, size_t length);

/* Shared binary value-type decoder used by imports and the current executor. */
int wasm_decode_byte_valtype(uint8_t byte, wasm_valtype *out);
int wasm_decode_valtype(wasm_reader *reader, wasm_valtype *out);

#endif
