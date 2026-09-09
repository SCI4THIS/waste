#ifndef WAST_TYPES_H
#define WAST_TYPES_H

#include <stdint.h>
#include <stddef.h>

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
#define REF_MATCH_NULL       255

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
#define WAST_MAX_PARAMS        128
#define WAST_MAX_LOCALS        2048
#define WAST_MAX_CODE_BYTES    65536
#define WAST_MAX_EXPORT_NAME   256
#define WAST_MAX_FUNCS         1024
#define WAST_MAX_RESULTS       32
#define WAST_MAX_ALTERNATIVES  4
#define WAST_MAX_ARGS          32
#define WAST_MAX_TYPES         128
#define WAST_MAX_TYPE_FIELDS   64
#define WAST_MAX_IMPORTS       64
#define WAST_MAX_GLOBALS       64
#define WAST_MAX_MEMORIES      32
#define WAST_MAX_TABLES        8
#define WAST_MAX_TAGS          64
#define WAST_MAX_DATA_SEGS     32
#define WAST_MAX_ELEM_SEGS     32
#define WAST_MAX_DATA_BYTES    65536
#define WAST_MAX_ELEM_REFS     256
#define WAST_MAX_ELEM_EXPR_BYTES 32

/* A parsed WAT function (code stored as pre-encoded bytes) */
typedef struct {
    char         export_name[WAST_MAX_EXPORT_NAME];
    int          has_export_name;
    char         id[WAST_MAX_EXPORT_NAME];         /* optional $name */
    char         import_module[WAST_MAX_EXPORT_NAME]; /* non-empty if imported */
    char         import_name[WAST_MAX_EXPORT_NAME];
    wasm_valtype params[WAST_MAX_PARAMS];
    int          param_count;
    int          has_inline_params;
    wasm_valtype results[WAST_MAX_RESULTS];
    int          result_count;
    int          has_inline_results;
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

typedef enum {
    WAST_TYPE_FUNC = 0,
    WAST_TYPE_STRUCT,
    WAST_TYPE_ARRAY
} wast_type_kind;

/* Type definition.  GC field metadata is retained so constexpr validation can
 * apply the same stack checks as the ordinary validator after parsing. */
typedef struct {
    char         id[WAST_MAX_EXPORT_NAME];
    wast_type_kind kind;
    /* Every core type belongs to a recursive group.  An ordinary `(type ...)`
     * is the one member of an implicit singleton group; members of an
     * explicit `(rec ...)` share these bounds.  Keeping the source grouping is
     * required for the spec's structural type equivalence rules. */
    uint32_t     rec_group_start;
    uint32_t     rec_group_size;
    wasm_valtype params[WAST_MAX_PARAMS];
    int          param_count;
    wasm_valtype results[WAST_MAX_RESULTS];
    int          result_count;
    wasm_valtype fields[WAST_MAX_TYPE_FIELDS];
    uint8_t      field_mutable[WAST_MAX_TYPE_FIELDS];
    uint8_t      field_packed[WAST_MAX_TYPE_FIELDS]; /* 0=valtype, 1=i8, 2=i16 */
    char         field_names[WAST_MAX_TYPE_FIELDS][64]; /* optional $name */
    int          field_count;
    int32_t      supertype;  /* -1 = none, >= 0 = parent type index */
    uint8_t      is_final;   /* 1 = sub final */
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
    uint64_t min;
    uint64_t max;
    int      has_max;
    int      is_shared;
    int      is_64;
} wast_limits;

typedef struct {
    uint64_t offset;
    uint32_t alignment;
} wast_memarg;

/* Global definition */
typedef struct {
    char         id[WAST_MAX_EXPORT_NAME];
    wasm_valtype valtype;
    int          is_mutable;
    uint8_t      init_expr[32]; /* raw binary instructions; encoder adds end */
    int          init_len;
    int          is_import;
    char         import_module[WAST_MAX_EXPORT_NAME];
    char         import_name[WAST_MAX_EXPORT_NAME];
    char         export_name[WAST_MAX_EXPORT_NAME];
    int          has_export_name;
} wast_global;

/* Table definition */
typedef struct {
    char         id[WAST_MAX_EXPORT_NAME];
    wasm_valtype reftype;
    wast_limits  limits;
    uint8_t      init_expr[32]; /* raw explicit initializer; encoder adds end */
    int          init_len;
    int          has_explicit_init;
    int          is_import;
    char         import_module[WAST_MAX_EXPORT_NAME];
    char         import_name[WAST_MAX_EXPORT_NAME];
    char         export_name[WAST_MAX_EXPORT_NAME];
    int          has_export_name;
} wast_table;

/* Memory definition */
typedef struct {
    char        id[WAST_MAX_EXPORT_NAME];
    wast_limits limits;
    int         is_import;
    char        import_module[WAST_MAX_EXPORT_NAME];
    char        import_name[WAST_MAX_EXPORT_NAME];
    char        export_name[WAST_MAX_EXPORT_NAME];
    int         has_export_name;
} wast_memory;

/* Exception tag metadata.  Tags are retained by the script linker even
 * while the execution engine does not yet expose throw/catch operations. */
typedef struct {
    char         id[WAST_MAX_EXPORT_NAME];
    wasm_valtype params[WAST_MAX_PARAMS];
    int          param_count;
    int          is_import;
    char         import_module[WAST_MAX_EXPORT_NAME];
    char         import_name[WAST_MAX_EXPORT_NAME];
    char         export_name[WAST_MAX_EXPORT_NAME];
    int          has_export_name;
} wast_tag;

/* Data segment (active or passive) */
typedef struct {
    int      is_passive;
    int      memory_index;
    char     name[WAST_MAX_EXPORT_NAME]; /* optional $id from (data $name ...) */
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
    uint8_t      ref_opcodes[WAST_MAX_ELEM_REFS]; /* ref.null/ref.func/global.get */
    wasm_valtype ref_types[WAST_MAX_ELEM_REFS]; /* heap type for ref.null */
    uint8_t      ref_exprs[WAST_MAX_ELEM_REFS][WAST_MAX_ELEM_EXPR_BYTES];
    int          ref_expr_lens[WAST_MAX_ELEM_REFS];
    int          ref_count;
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

    /* Exception tags (link-time metadata; not emitted into the MVP binary). */
    wast_tag    tags[WAST_MAX_TAGS];
    int         tag_count;

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
    char register_target[WAST_MAX_EXPORT_NAME]; /* optional $id in register */
    int  is_definition; /* (module definition ...): validate/store as a template */
    char instance_of[WAST_MAX_EXPORT_NAME]; /* definition named by module instance */
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

typedef enum {
    WAST_RAW_NONE = 0,
    WAST_RAW_BINARY,
    WAST_RAW_QUOTE
} wast_raw_module_kind;

typedef struct {
    wast_raw_module_kind kind;
    uint8_t *bytes;
    size_t length;
} wast_raw_module;

typedef struct {
    int line;
    int column;
    int fold_depth;
    int offset_overflow; /* set when offset=/align= exceeds its encoded range */
    char **strings;      /* parse-lifetime storage for unbounded STRING tokens */
    size_t string_count;
    size_t string_capacity;
} wast_lex_state;

/* One module + index range into flat assertion pool */
typedef struct {
    wast_module module;
    wast_raw_module raw_module;
    int         assertion_start; /* index into wast_script.assertions[] */
    int         assertion_count;
    int         has_module_assertion;
    wast_assert_kind module_assert_kind;
    char        expected_module_error[WAST_MAX_EXPORT_NAME];
    int         has_validation_error;
    char        validation_error[WAST_MAX_EXPORT_NAME];
} wast_group;

/* Complete parsed WAST script */
typedef struct {
    wast_group   *groups;
    int           group_count;
    int           group_capacity;
    wast_assertion *assertions; /* flat pool */
    int            assertion_count;
    int            assertion_capacity;
    int            command_count;
    wast_raw_module *raw_modules;
    int            raw_module_count;
    int            raw_module_cursor;
    int            strict_wat_mode;
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
