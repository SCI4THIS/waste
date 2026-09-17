#ifndef WASTE_H
#define WASTE_H

#include <stddef.h>
#include <stdint.h>

/* ---- Value types ---- */

typedef struct { uint8_t bytes[16]; } wasm_v128;

typedef enum {
    WASM_VALTYPE_I32 = 0,
    WASM_VALTYPE_I64,
    WASM_VALTYPE_F32,
    WASM_VALTYPE_F64,
    WASM_VALTYPE_V128,
    WASM_VALTYPE_FUNCREF,
    WASM_VALTYPE_EXTERNREF,
    WASM_VALTYPE_FUNCREF_NONNULL,
    WASM_VALTYPE_EXTERNREF_NONNULL,
    WASM_VALTYPE_ANYREF,
    WASM_VALTYPE_EQREF,
    WASM_VALTYPE_I31REF,
    WASM_VALTYPE_STRUCTREF,
    WASM_VALTYPE_ARRAYREF,
    WASM_VALTYPE_ANYREF_NONNULL,
    WASM_VALTYPE_EQREF_NONNULL,
    WASM_VALTYPE_I31REF_NONNULL,
    WASM_VALTYPE_STRUCTREF_NONNULL,
    WASM_VALTYPE_ARRAYREF_NONNULL,
    WASM_VALTYPE_EXNREF,
    WASM_VALTYPE_EXNREF_NONNULL,
    WASM_VALTYPE_NULLREF,
    WASM_VALTYPE_NULLFUNCREF,
    WASM_VALTYPE_NULLEXNREF,
    WASM_VALTYPE_NULLEXTERNREF
} wasm_valtype;

#define WASM_VALTYPE_TYPE_REF_NULL_BASE 0x100
#define WASM_VALTYPE_TYPE_REF_BASE      0x200
#define WASM_VALTYPE_TYPE_REF_LIMIT     0x300
#define WASM_VALTYPE_IS_TYPE_REF(t) \
    ((unsigned)(t) >= WASM_VALTYPE_TYPE_REF_NULL_BASE && \
     (unsigned)(t) < WASM_VALTYPE_TYPE_REF_LIMIT)
#define WASM_VALTYPE_TYPE_REF_INDEX(t) ((unsigned)(t) & 0xffu)

typedef struct {
    wasm_valtype type;
    union {
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;
        wasm_v128 v128;
        uint32_t ref;
    };
    uint8_t nan_mode[16];
} wasm_value;

typedef wasm_valtype waste_value_type;
typedef wasm_v128 waste_v128;
typedef wasm_value waste_value;

/* ---- Error types ---- */

typedef enum {
    WASTE_OK = 0,
    WASTE_ERROR_MALFORMED,
    WASTE_ERROR_UNSUPPORTED,
    WASTE_ERROR_TRAP,
    WASTE_ERROR_EXCEPTION,
    WASTE_ERROR_NOT_FOUND,
    WASTE_ERROR_EXIT,
    WASTE_YIELD
} waste_status;

typedef struct {
    waste_status status;
    size_t offset;
    char message[256];
} waste_error;

/* ---- Engine API ---- */

typedef struct waste_module waste_module;
typedef struct waste_instance waste_instance;

waste_status waste_module_decode(const uint8_t *bytes, size_t size,
                                 waste_module **module_out,
                                 waste_error *error);
void waste_module_delete(waste_module *module);

waste_status waste_instance_create(const waste_module *module,
                                   waste_instance **instance_out,
                                   waste_error *error);
waste_status waste_instance_load(const uint8_t *bytes, size_t size,
                                 waste_instance **instance_out,
                                 waste_error *error);
void waste_instance_delete(waste_instance *instance);

waste_status waste_instance_find_function(const waste_instance *instance,
                                          const char *name,
                                          uint32_t *function_out,
                                          waste_error *error);
waste_status waste_instance_invoke(waste_instance *instance,
                                   uint32_t function,
                                   const waste_value *arguments,
                                   size_t argument_count,
                                   waste_value *results,
                                   size_t result_capacity,
                                   size_t *result_count,
                                   waste_error *error);

#endif /* WASTE_H */
