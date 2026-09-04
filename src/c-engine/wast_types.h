#ifndef WAST_TYPES_H
#define WAST_TYPES_H

#include <stdint.h>

/* 16-byte v128 value */
typedef struct { uint8_t bytes[16]; } wasm_v128;

/* Value type enum */
typedef enum {
    WASM_VALTYPE_I32 = 0,
    WASM_VALTYPE_I64,
    WASM_VALTYPE_F32,
    WASM_VALTYPE_F64,
    WASM_VALTYPE_V128,
    WASM_VALTYPE_FUNCREF,
    WASM_VALTYPE_EXTERNREF,
    WASM_VALTYPE_FUNCREF_NONNULL,
    WASM_VALTYPE_EXTERNREF_NONNULL
} wasm_valtype;

/* Indexed heap references retain both nullability and their type index. */
#define WASM_VALTYPE_TYPE_REF_NULL_BASE 0x100
#define WASM_VALTYPE_TYPE_REF_BASE      0x200
#define WASM_VALTYPE_TYPE_REF_LIMIT     0x300
#define WASM_VALTYPE_IS_TYPE_REF(t) \
    ((unsigned)(t) >= WASM_VALTYPE_TYPE_REF_NULL_BASE && \
     (unsigned)(t) < WASM_VALTYPE_TYPE_REF_LIMIT)
#define WASM_VALTYPE_TYPE_REF_INDEX(t) ((unsigned)(t) & 0xffu)

/* NaN match mode stored in nan_mode[] */
#define NAN_MATCH_EXACT      0
#define NAN_MATCH_F32_CANON  1
#define NAN_MATCH_F32_ARITH  2
#define NAN_MATCH_F64_CANON  3
#define NAN_MATCH_F64_ARITH  4

/* Test argument / expected result value */
typedef struct {
    wasm_valtype type;
    union {
        int32_t  i32;
        int64_t  i64;
        float    f32;
        double   f64;
        wasm_v128 v128;
        uint32_t ref;
    };
    uint8_t nan_mode[16]; /* per-lane NaN matching (scalar: nan_mode[0] only) */
} wasm_value;

/* Assertion kinds */
typedef enum {
    WAST_ASSERT_RETURN = 0,
    WAST_ASSERT_TRAP,
    WAST_ASSERT_EXHAUSTION,
    WAST_ASSERT_INVALID,    /* expect load to fail */
    WAST_ASSERT_MALFORMED,  /* expect parse to fail */
    WAST_ASSERT_UNLINKABLE
} wast_assert_kind;

typedef enum {
    WAST_ACTION_INVOKE = 0,
    WAST_ACTION_GET
} wast_action_kind;

/* Size limits */
#define WAST_MAX_PARAMS        16
#define WAST_MAX_LOCALS        64
#define WAST_MAX_CODE_BYTES    16384
#define WAST_MAX_EXPORT_NAME   128
#define WAST_MAX_FUNCS         1024
#define WAST_MAX_RESULTS       8
#define WAST_MAX_ALTERNATIVES  4
#define WAST_MAX_ARGS          8
#define WAST_MAX_ASSERTIONS    4096   /* flat pool across whole script */
#define WAST_MAX_GROUPS        128
#define WAST_MAX_TYPES         64
#define WAST_MAX_IMPORTS       64
#define WAST_MAX_GLOBALS       64
#define WAST_MAX_MEMORIES      4
#define WAST_MAX_TABLES        8
#define WAST_MAX_DATA_SEGS     32
#define WAST_MAX_ELEM_SEGS     32
#define WAST_MAX_DATA_BYTES    65536
#define WAST_MAX_ELEM_REFS     256

/* A parsed WAT function (code stored as pre-encoded bytes) */
typedef struct {
    char         export_name[WAST_MAX_EXPORT_NAME];
    char         id[WAST_MAX_EXPORT_NAME];         /* optional $name */
    char         import_module[WAST_MAX_EXPORT_NAME]; /* non-empty if imported */
    char         import_name[WAST_MAX_EXPORT_NAME];
    wasm_valtype params[WAST_MAX_PARAMS];
    int          param_count;
    wasm_valtype results[WAST_MAX_RESULTS];
    int          result_count;
    int          type_index;  /* explicit type ref, -1 if none */
    wasm_valtype locals[WAST_MAX_LOCALS];  /* extra locals beyond params */
    int          local_count;
    uint8_t      code[WAST_MAX_CODE_BYTES]; /* binary-encoded body (no local decl header) */
    int          code_len;
    int          is_import;

    /* Legacy SIMD path — kept for compatibility */
    uint32_t     _simd_instrs_unused[128*4]; /* old wasm_instr[] placeholder */
    int          _simd_instr_count;
} wast_func;

/* Function type definition (from (type ...) declarations) */
typedef struct {
    char         id[WAST_MAX_EXPORT_NAME];
    wasm_valtype params[WAST_MAX_PARAMS];
    int          param_count;
    wasm_valtype results[WAST_MAX_RESULTS];
    int          result_count;
} wast_type;

/* Import kinds */
typedef enum {
    WAST_IMPORT_FUNC   = 0,
    WAST_IMPORT_TABLE  = 1,
    WAST_IMPORT_MEMORY = 2,
    WAST_IMPORT_GLOBAL = 3
} wast_import_kind;

/* Memory limits */
typedef struct {
    uint32_t min;
    uint32_t max;
    int      has_max;
    int      is_shared;
} wast_limits;

/* Global definition */
typedef struct {
    char         id[WAST_MAX_EXPORT_NAME];
    wasm_valtype valtype;
    int          is_mutable;
    uint8_t      init_expr[32]; /* binary-encoded init expression */
    int          init_len;
    int          is_import;
    char         import_module[WAST_MAX_EXPORT_NAME];
    char         import_name[WAST_MAX_EXPORT_NAME];
    char         export_name[WAST_MAX_EXPORT_NAME];
} wast_global;

/* Table definition */
typedef struct {
    char         id[WAST_MAX_EXPORT_NAME];
    wasm_valtype reftype;
    wast_limits  limits;
    int          is_import;
    char         import_module[WAST_MAX_EXPORT_NAME];
    char         import_name[WAST_MAX_EXPORT_NAME];
    char         export_name[WAST_MAX_EXPORT_NAME];
} wast_table;

/* Memory definition */
typedef struct {
    char        id[WAST_MAX_EXPORT_NAME];
    wast_limits limits;
    int         is_import;
    char        import_module[WAST_MAX_EXPORT_NAME];
    char        import_name[WAST_MAX_EXPORT_NAME];
    char        export_name[WAST_MAX_EXPORT_NAME];
} wast_memory;

/* Data segment (active or passive) */
typedef struct {
    int      is_passive;
    int      memory_index;
    uint8_t  offset_expr[32]; /* binary init expr for active offset */
    int      offset_len;
    uint8_t *bytes;           /* heap-allocated; must be freed by encoder; init to NULL */
    int      len;
} wast_data_seg;

/* Element segment */
typedef struct {
    int          is_passive;
    int          is_declarative;
    int          table_index;
    uint8_t      offset_expr[32];
    int          offset_len;
    wasm_valtype reftype;
    uint32_t     refs[WAST_MAX_ELEM_REFS]; /* func indices */
    int          ref_count;
    /* raw init exprs for non-func refs (not yet supported, just func refs) */
} wast_elem_seg;

/* Additional (export ...) outside function bodies */
typedef struct {
    char     name[WAST_MAX_EXPORT_NAME];
    uint32_t index;
    int      kind; /* 0=func, 1=table, 2=memory, 3=global */
} wast_export;

#define WAST_MAX_EXPORTS 128

/* Complete parsed WAT module */
typedef struct {
    /* Types */
    wast_type   types[WAST_MAX_TYPES];
    int         type_count;

    /* Functions (includes imported funcs with is_import=1) */
    wast_func  *funcs;
    int         func_capacity;
    int         func_count;

    /* Memories */
    wast_memory memories[WAST_MAX_MEMORIES];
    int         memory_count;

    /* Globals */
    wast_global globals[WAST_MAX_GLOBALS];
    int         global_count;

    /* Tables */
    wast_table  tables[WAST_MAX_TABLES];
    int         table_count;

    /* Data segments */
    wast_data_seg data[WAST_MAX_DATA_SEGS];
    int           data_count;

    /* Element segments */
    wast_elem_seg elem[WAST_MAX_ELEM_SEGS];
    int           elem_count;

    /* Exports (standalone export declarations) */
    wast_export exports[WAST_MAX_EXPORTS];
    int         export_count;

    /* Start function */
    int start_func; /* -1 = none */

    /* Module identity */
    char id[WAST_MAX_EXPORT_NAME];
    char register_name[WAST_MAX_EXPORT_NAME]; /* for (register "name") */
} wast_module;

/* A single test assertion */
typedef struct {
    wast_assert_kind kind;
    wast_action_kind action_kind;
    char             func_name[WAST_MAX_EXPORT_NAME];
    char             module_id[WAST_MAX_EXPORT_NAME];
    wasm_value       args[WAST_MAX_ARGS];
    int              arg_count;
    wasm_value       alternatives[WAST_MAX_ALTERNATIVES][WAST_MAX_RESULTS];
    int              alt_count;
    int              result_count;
    char             expected_trap[WAST_MAX_EXPORT_NAME]; /* for assert_trap */
} wast_assertion;

typedef struct {
    int line;
    int column;
} wast_lex_state;

/* One module + index range into flat assertion pool */
typedef struct {
    wast_module module;
    int         assertion_start; /* index into wast_script.assertions[] */
    int         assertion_count;
    int         has_module_assertion;
    wast_assert_kind module_assert_kind;
    char        expected_module_error[WAST_MAX_EXPORT_NAME];
} wast_group;

/* Complete parsed WAST script */
typedef struct {
    wast_group    groups[WAST_MAX_GROUPS];
    int           group_count;
    wast_assertion assertions[WAST_MAX_ASSERTIONS]; /* flat pool */
    int            assertion_count;
    char           error[256];
} wast_script;

/* Backward-compat: wast_instrs field kept but code[] is used for new path */
/* Legacy struct for SIMD only */
typedef struct {
    uint32_t opcode;
    uint32_t simd_op;
    uint32_t u32_imm;
    wasm_v128 v128_imm;
} wasm_instr;

#define WAST_MAX_INSTRS 128

#endif /* WAST_TYPES_H */
