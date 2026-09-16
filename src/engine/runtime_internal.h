#ifndef WASTE_RUNTIME_INTERNAL_H
#define WASTE_RUNTIME_INTERNAL_H

#include "engine_internal.h"

#include <stdint.h>

enum {
    EXEC_MAX_TYPES = 128,
    EXEC_MAX_FUNCS = 8192,
    EXEC_MAX_EXPORTS = 65536,
    EXEC_MAX_NAME = WAST_MAX_EXPORT_NAME,
    EXEC_MAX_INSTRS = 4096,
    EXEC_MAX_STACK = 256,
    EXEC_MAX_LOCALS = WAST_MAX_LOCALS,
    EXEC_MAX_CONTROL = 256,
    EXEC_MAX_CALL_DEPTH = 256,
    EXEC_MAX_CALL_ARGS = 256,
    EXEC_MAX_GLOBALS = 2048,
    EXEC_MAX_TABLES = 16,
    EXEC_PAGE_SIZE = 65536,
    EXEC_MAX_GC_OBJECT_BYTES = 64 * 1024 * 1024,
};

typedef struct {
    uint8_t kind;
    uint32_t tag_index;
    uint32_t depth;
} exec_catch;

enum {
    EXEC_INSTR_VALIDATED = 1u << 0,
    EXEC_INSTR_RESOLVED_TYPE = 1u << 1,
    EXEC_INSTR_RESOLVED_BRANCH = 1u << 2,
    EXEC_INSTR_RESOLVED_IMMEDIATE = 1u << 3,
};

typedef struct {
    uint32_t opcode;
    uint32_t simd_op;
    uint32_t u32_imm;
    uint64_t u64_imm;
    uint32_t memory_index;
    uint32_t source_memory_index;
    uint32_t alignment;
    uint32_t lane_index;
    wasm_v128 v128_imm;
    int32_t block_type_index;
    wasm_valtype block_result_type;
    uint8_t has_block_result_type;
    exec_catch *catches;
    uint32_t catch_count;

    /* Filled exactly once by wasm_validate_function, then read-only. */
    uint32_t validation_flags;
    uint32_t resolved_type_index;
    uint32_t resolved_target;
    wasm_valtype resolved_value_type;
} exec_instr;

typedef struct {
    wast_type_kind kind;
    uint32_t type_index;
    uint32_t length;
    wasm_value *values;
} exec_gc_object;

typedef struct {
    const exec_tag *tag;
    wasm_value payload[WAST_MAX_PARAMS];
    int payload_count;
} exec_exception_object;

typedef struct {
    wasm_valtype params[EXEC_MAX_LOCALS];
    wasm_valtype results[WAST_MAX_RESULTS];
    int param_count;
    int result_count;
    wast_type_kind kind;
    uint32_t rec_group_start;
    uint32_t rec_group_size;
    wasm_valtype fields[WAST_MAX_TYPE_FIELDS];
    uint8_t field_mutable[WAST_MAX_TYPE_FIELDS];
    uint8_t field_packed[WAST_MAX_TYPE_FIELDS];
    int field_count;
    int32_t supertype;
    uint8_t is_final;
} exec_func_type;

typedef struct {
    uint32_t type_index;
    wasm_valtype locals[EXEC_MAX_LOCALS];
    uint32_t local_count;
    exec_instr *code;
    uint32_t code_size;
    uint8_t validated;
} exec_func;

typedef struct {
    char name[EXEC_MAX_NAME];
    uint32_t index;
    uint8_t kind;
} exec_export;

typedef struct exec_stack {
    wasm_value vals[EXEC_MAX_STACK];
    int top;
} exec_stack;

typedef struct exec_control {
    uint32_t kind;
    uint32_t start_pc;
    uint32_t end_pc;
    int stack_height;
    int branch_arity;
    int end_arity;
} exec_control;

typedef struct exec_jump_snapshot {
    uint8_t valid;
    uint32_t environment;
    uint32_t depth;
    uint32_t func_idx;
    uint32_t pc;
    uint64_t frame_generation;
    exec_stack stack;
    exec_control controls[EXEC_MAX_CONTROL];
    int control_top;
    wasm_value *locals;
    uint32_t local_count;
} exec_jump_snapshot;

typedef struct {
    uint8_t valid;
    uint32_t func_idx;
    uint32_t pc;
    int control_top;
} exec_yield_frame;

struct waste_exec_engine {
    exec_func_type *types;
    uint32_t type_count;
    exec_func *funcs;
    uint32_t func_count;
    uint32_t import_func_count;
    uint32_t import_func_types[EXEC_MAX_FUNCS];
    exec_host_func import_funcs[EXEC_MAX_FUNCS];
    void *import_host_data[EXEC_MAX_FUNCS];
    exec_host_control import_controls[EXEC_MAX_FUNCS];
    exec_export *exports;
    uint32_t export_count;
    exec_global *globals[EXEC_MAX_GLOBALS];
    exec_global owned_globals[EXEC_MAX_GLOBALS];
    uint32_t global_count;
    uint32_t import_global_count;
    exec_memory *memories[WAST_MAX_MEMORIES];
    exec_memory owned_memories[WAST_MAX_MEMORIES];
    uint8_t owns_memories[WAST_MAX_MEMORIES];
    uint32_t memory_count;
    uint32_t import_memory_count;
    exec_memory *memory;
    exec_table *tables[EXEC_MAX_TABLES];
    exec_table owned_tables[EXEC_MAX_TABLES];
    uint32_t table_count;
    uint32_t import_table_count;
    exec_tag *tags[WAST_MAX_TAGS];
    exec_tag owned_tags[WAST_MAX_TAGS];
    uint32_t tag_types[WAST_MAX_TAGS];
    uint32_t tag_count;
    uint32_t import_tag_count;
    uint32_t start_func;
    int has_start;
    uint32_t declared_data_count;
    uint8_t has_data_count;
    uint8_t uses_data_count_instruction;
    uint8_t declared_funcs[EXEC_MAX_FUNCS];
    exec_table_element *elem_values[WAST_MAX_ELEM_SEGS];
    uint32_t elem_lengths[WAST_MAX_ELEM_SEGS];
    wasm_valtype elem_types[WAST_MAX_ELEM_SEGS];
    uint8_t elem_dropped[WAST_MAX_ELEM_SEGS];
    uint32_t elem_count;
    uint8_t *data_segs[WAST_MAX_DATA_SEGS];
    uint32_t data_seg_lengths[WAST_MAX_DATA_SEGS];
    uint8_t data_dropped[WAST_MAX_DATA_SEGS];
    uint32_t data_count;
    uint8_t instantiation_trapped;
    uint32_t next_opaque_ref;
    exec_gc_object *gc_objects;
    uint32_t gc_object_count;
    uint32_t gc_object_capacity;
    exec_exception_object *exception_objects;
    uint32_t exception_object_count;
    uint32_t exception_object_capacity;
    char instantiation_error[256];
    wasm_value *local_frames[EXEC_MAX_CALL_DEPTH];
    exec_stack *operand_frames[EXEC_MAX_CALL_DEPTH];
    exec_control *control_frames[EXEC_MAX_CALL_DEPTH];
    uint32_t local_frame_capacities[EXEC_MAX_CALL_DEPTH];
    uint32_t active_call_depth;
    uint64_t frame_generations[EXEC_MAX_CALL_DEPTH];
    exec_jump_snapshot *jump_snapshots;
    uint32_t jump_snapshot_count;
    uint32_t jump_snapshot_capacity;
    exec_yield_frame yield_frames[EXEC_MAX_CALL_DEPTH];
};

/* Return values for opcode dispatch execute handlers.
 * Non-negative values match exec_status and propagate to the caller.
 * Negative values are internal actions consumed by the instruction loop. */
enum {
    WASM_DISPATCH_RETURN    = -1, /* function complete, results on stack */
    WASM_DISPATCH_TAIL_CALL = -2, /* restart with updated context fields */
};

/* One active interpreter frame. Opcode helpers receive this bounded view
 * instead of depending on evaluator-local variables or parser state. */
typedef struct {
    waste_exec_engine *engine;
    exec_func *function;
    exec_func_type *type;
    exec_stack *operand_stack;
    exec_control *controls;
    wasm_value *locals;
    uint32_t local_count;
    uint32_t function_index;
    uint32_t depth;
    uint64_t frame_generation;
    /* Extended fields for dispatch handlers — set by the instruction loop
     * so that handlers can modify evaluator-local state. */
    int *control_top;
    uint32_t *pc;
    exec_error *error;
    wasm_value *results;
    int *result_count;
    wasm_value *tail_args;
    int *tail_arg_count;
} waste_exec_context;

exec_status exec_raise(exec_error *error, const exec_tag *tag,
                       const wasm_value *payload, int payload_count,
                       waste_exec_engine *owner, uint32_t reference);

int stack_push(exec_stack *stack, wasm_value value);
int stack_pop(exec_stack *stack, wasm_value *value);
wasm_value i32_value(uint32_t bits);
wasm_value i64_value(uint64_t bits);
int address_value(const wasm_value *value, int is_64, uint64_t *out);
uint64_t load_le(const uint8_t *memory, size_t address, uint32_t width);
void store_le(uint8_t *memory, size_t address, uint64_t value,
              uint32_t width);
exec_status memory_address(exec_memory *memory, uint64_t base,
                           uint64_t offset, uint32_t width,
                           size_t *address, exec_error *error);
exec_status exec_memory_instruction(waste_exec_context *context,
                                    const exec_instr *instruction,
                                    exec_error *error);
exec_status exec_memory_bulk(waste_exec_context *context,
                             const exec_instr *instruction,
                             uint32_t subopcode, exec_error *error);
exec_status exec_table_get_set(waste_exec_context *context,
                               const exec_instr *instruction,
                               exec_error *error);
exec_status exec_table_bulk(waste_exec_context *context,
                            const exec_instr *instruction,
                            uint32_t subopcode, exec_error *error);

wasm_valtype reference_dynamic_type(const wasm_value *value);
void set_reference_dynamic_type(wasm_value *value,
                                wasm_valtype dynamic_type);
wasm_value default_value(wasm_valtype type);
exec_gc_object *gc_object(waste_exec_engine *engine,
                          const wasm_value *value);
int gc_allocate(waste_exec_engine *engine, wast_type_kind kind,
                uint32_t type_index, uint32_t length,
                wasm_value *out, exec_error *error);
int reference_matches_heap(waste_exec_engine *engine,
                           const wasm_value *value,
                           int32_t heap_type, int nullable);
wasm_value packed_field_value(wasm_value value, uint8_t packed);
int gc_push(exec_stack *stack, wasm_value value, exec_error *error);
exec_exception_object *exception_object(waste_exec_engine *engine,
                                        const wasm_value *value);
int handle_exception(waste_exec_context *ctx,
                     const exec_tag *tag, const wasm_value *payload,
                     int payload_count, waste_exec_engine *source_owner,
                     uint32_t source_reference);

int exec_standard_simd_integer(uint32_t operation, uint32_t lane_index,
                               const wasm_v128 *immediate,
                               exec_stack *stack, exec_error *error);
int exec_standard_simd_float(uint32_t operation, exec_stack *stack,
                             exec_error *error);
int simd_pop(exec_stack *stack, wasm_value *value);
wasm_value simd_zero(void);
void simd_set_lane(wasm_value *value, uint32_t lane, uint32_t width,
                   uint64_t bits);
wasm_value exec_i8x16_laneselect(wasm_value a, wasm_value b, wasm_value c);
wasm_value exec_bitselect(wasm_value a, wasm_value b, wasm_value c);
wasm_value exec_i8x16_relaxed_swizzle(wasm_value a, wasm_value b);
wasm_value exec_i8x16_eq(wasm_value a, wasm_value b);
wasm_value exec_i16x8_eq(wasm_value a, wasm_value b);
wasm_value exec_i32x4_eq(wasm_value a, wasm_value b);
wasm_value exec_i64x2_eq(wasm_value a, wasm_value b);
wasm_value exec_f32x4_eq(wasm_value a, wasm_value b);
wasm_value exec_f64x2_eq(wasm_value a, wasm_value b);
wasm_value exec_f32x4_relaxed_min(wasm_value a, wasm_value b);
wasm_value exec_f32x4_relaxed_max(wasm_value a, wasm_value b);
wasm_value exec_f64x2_relaxed_min(wasm_value a, wasm_value b);
wasm_value exec_f64x2_relaxed_max(wasm_value a, wasm_value b);
wasm_value exec_f32x4_relaxed_madd(wasm_value a, wasm_value b,
                                    wasm_value c);
wasm_value exec_f32x4_relaxed_nmadd(wasm_value a, wasm_value b,
                                     wasm_value c);
wasm_value exec_f64x2_relaxed_madd(wasm_value a, wasm_value b,
                                    wasm_value c);
wasm_value exec_f64x2_relaxed_nmadd(wasm_value a, wasm_value b,
                                     wasm_value c);
wasm_value exec_i32x4_relaxed_trunc_f32x4_s(wasm_value a);
wasm_value exec_i32x4_relaxed_trunc_f32x4_u(wasm_value a);
wasm_value exec_i32x4_relaxed_trunc_f64x2_s_zero(wasm_value a);
wasm_value exec_i32x4_relaxed_trunc_f64x2_u_zero(wasm_value a);
wasm_value exec_i16x8_relaxed_q15mulr_s(wasm_value a, wasm_value b);
wasm_value exec_i16x8_relaxed_dot_i8x16_i7x16_s(wasm_value a,
                                                 wasm_value b);
wasm_value exec_i32x4_relaxed_dot_i8x16_i7x16_add_s(
    wasm_value a, wasm_value b, wasm_value c);
int exec_gc_instruction(waste_exec_engine *engine,
                        const exec_instr *instruction,
                        exec_stack *stack, exec_error *error);
exec_status exec_sat_trunc(uint32_t subopcode, exec_stack *stack,
                           exec_error *error);
uint32_t trunc_sat_i32_s_f32(float value);
uint32_t trunc_sat_i32_u_f32(float value);
uint32_t trunc_sat_i32_s_f64(double value);
uint32_t trunc_sat_i32_u_f64(double value);
uint64_t trunc_sat_i64_s_f32(float value);
uint64_t trunc_sat_i64_u_f32(float value);
uint64_t trunc_sat_i64_s_f64(double value);
uint64_t trunc_sat_i64_u_f64(double value);

void runtime_free_jump_snapshots(waste_exec_engine *engine);

#endif
