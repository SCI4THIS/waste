#ifndef WASTE_WASM_MODULE_H
#define WASTE_WASM_MODULE_H

#include "wat/types.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    WASM_IMPORT_FUNCTION = 0,
    WASM_IMPORT_TABLE = 1,
    WASM_IMPORT_MEMORY = 2,
    WASM_IMPORT_GLOBAL = 3,
    WASM_IMPORT_TAG = 4
} wasm_import_kind;

typedef struct {
    uint64_t minimum;
    uint64_t maximum;
    uint32_t flags;
} wasm_import_limits;

typedef struct {
    char module[WAST_MAX_EXPORT_NAME];
    char name[WAST_MAX_EXPORT_NAME];
    wasm_import_kind kind;
    union {
        struct {
            uint32_t type_index;
        } function;
        struct {
            wasm_valtype element_type;
            wasm_import_limits limits;
        } table;
        struct {
            wasm_import_limits limits;
        } memory;
        struct {
            wasm_valtype value_type;
            uint8_t mutable_;
        } global;
        struct {
            uint32_t attribute;
            uint32_t type_index;
        } tag;
    } descriptor;
} wasm_import;

typedef struct {
    size_t payload_offset;
    uint32_t payload_size;
    uint8_t id;
} wasm_section;

/* An uninstantiated module owns its immutable binary representation and the
 * declarations needed before linking. Runtime state never points into the
 * caller's source buffer. */
typedef struct {
    const uint8_t *source;
    size_t source_size;
    uint8_t *owned_source;
    wasm_import *imports;
    uint32_t import_count;
    wasm_section *sections;
    uint32_t section_count;
    uint8_t fully_decoded;
} wasm_module;

void wasm_module_init(wasm_module *module);
void wasm_module_dispose(wasm_module *module);

#endif
