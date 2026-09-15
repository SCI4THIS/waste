#ifndef WASTE_WAST_CONTEXT_H
#define WASTE_WAST_CONTEXT_H

#include "wat/types.h"

#include <stddef.h>
#include <stdint.h>

#define WAST_MAX_LABEL_DEPTH 256
#define WAST_MAX_FUNC_FIXUPS 4096
#define WAST_MAX_REC_FIXUPS 256
#define WAST_MAX_BR_TABLE_LABELS 65536
#define WAST_LITERAL_SCRATCH_SIZE 4096

typedef struct {
    uint32_t func_index;
    uint32_t code_offset;
    int line;
    int column;
    char name[WAST_MAX_EXPORT_NAME];
} wast_func_fixup;

typedef struct {
    uint32_t func_index;
    int line;
    int column;
    char name[WAST_MAX_EXPORT_NAME];
} wast_type_fixup;

typedef enum {
    IDX_FUNC,
    IDX_TYPE,
    IDX_GLOBAL,
    IDX_TABLE,
    IDX_MEMORY,
    IDX_ELEM,
    IDX_TAG,
    IDX_DATA
} wast_index_space;

typedef struct {
    wast_index_space space;
    uint32_t func_index;
    uint32_t code_offset;
    int line;
    int column;
    char name[WAST_MAX_EXPORT_NAME];
} wast_code_index_fixup;

typedef enum {
    META_ELEM_FUNC,
    META_ELEM_GLOBAL,
    META_ELEM_TABLE,
    META_EXPORT,
    META_START,
    META_GLOBAL_INIT,
    META_TABLE_INIT
} wast_meta_fixup_kind;

typedef struct {
    wast_meta_fixup_kind kind;
    wast_index_space space;
    uint32_t first;
    uint32_t second;
    int line;
    int column;
    char name[WAST_MAX_EXPORT_NAME];
} wast_meta_fixup;

typedef struct {
    uint32_t type_index;
    int slot;
    int location;
    int nullable;
    char name[WAST_MAX_EXPORT_NAME];
} wast_rec_type_fixup;

typedef struct {
    uint8_t kind;
    uint32_t tag;
    uint32_t depth;
} wast_parsed_catch;

typedef struct wat_context {
    wast_lex_state lex;
    char literal_scratch[WAST_LITERAL_SCRATCH_SIZE];
    char lexer_error[256];
    int skip_annotation_depth;
    int skip_comment_depth;
    int skip_in_string;
    int skip_line_comment;
    int skip_return_paren;
    int paren_line;
    int paren_column;
    int inline_module;
    int inline_token_emitted;
    int strict_wat_mode;
    int command_scan;
    int command_scan_inline;
    int command_scan_started;
    int command_scan_finished;
    int command_scan_depth;
    size_t command_start_offset;
    size_t command_end_offset;
    unsigned command_start_line;
    int numeric_kind; /* 0=none, 1=integer, 2=float */
    int numeric_bits;
    int numeric_remaining;
    int v128_const_pending;
    wast_raw_module raw_payload;
    size_t raw_payload_capacity;

    wast_func cur_func;
    int in_func;
    int cur_group;
    uint32_t cur_func_index;

    char labels[WAST_MAX_LABEL_DEPTH][WAST_MAX_EXPORT_NAME];
    int label_depth;
    char local_names[WAST_MAX_PARAMS + WAST_MAX_LOCALS]
                    [WAST_MAX_EXPORT_NAME];
    int local_name_count;

    char func_names[WAST_MAX_FUNCS][WAST_MAX_EXPORT_NAME];
    int func_name_count;
    char global_names[WAST_MAX_GLOBALS][WAST_MAX_EXPORT_NAME];
    int global_name_count;
    char type_names[WAST_MAX_TYPES][WAST_MAX_EXPORT_NAME];
    uint32_t type_name_indices[WAST_MAX_TYPES];
    int type_name_count;
    int in_rec_group;
    int parsing_type_definition;
    uint32_t rec_group_start;
    char table_names[WAST_MAX_TABLES][WAST_MAX_EXPORT_NAME];
    int table_name_count;
    char memory_names[WAST_MAX_MEMORIES][WAST_MAX_EXPORT_NAME];
    int memory_name_count;
    char data_names[WAST_MAX_DATA_SEGS][WAST_MAX_EXPORT_NAME];
    int data_name_count;
    char elem_names[WAST_MAX_ELEM_SEGS][WAST_MAX_EXPORT_NAME];
    int elem_name_count;
    char tag_names[WAST_MAX_TAGS][WAST_MAX_EXPORT_NAME];
    int tag_name_count;

    wast_tag cur_tag;
    wast_func_fixup func_fixups[WAST_MAX_FUNC_FIXUPS];
    int func_fixup_count;
    wast_type_fixup type_fixups[WAST_MAX_FUNCS];
    int type_fixup_count;
    wast_code_index_fixup code_fixups[WAST_MAX_FUNC_FIXUPS];
    int code_fixup_count;
    wast_meta_fixup meta_fixups[WAST_MAX_FUNC_FIXUPS];
    int meta_fixup_count;
    wast_rec_type_fixup rec_fixups[WAST_MAX_REC_FIXUPS];
    int rec_fixup_count;

    wast_assertion cur_assert;
    int in_assert;
    char invoke_name[WAST_MAX_EXPORT_NAME];
    int module_assert_action;

    uint32_t brtable_labels[WAST_MAX_BR_TABLE_LABELS];
    int brtable_count;
    wast_parsed_catch try_catches[WAST_MAX_TAGS];
    int try_catch_count;
    wasm_valtype select_result_types[WAST_MAX_RESULTS];
    int select_result_count;
    uint32_t lane_imms[32];
    int lane_imm_count;

    wasm_valtype inline_params[WAST_MAX_PARAMS];
    int inline_param_count;
    wasm_valtype inline_results[WAST_MAX_RESULTS];
    int inline_result_count;

    char import_module[WAST_MAX_EXPORT_NAME];
    char import_name[WAST_MAX_EXPORT_NAME];
    int export_kind;
    uint32_t export_index;
    char instance_args[2][WAST_MAX_EXPORT_NAME];
    int instance_arg_count;

    wast_global cur_global;
    wast_type cur_type;
    wast_data_seg cur_data;
    wast_elem_seg cur_elem;
    uint8_t *constexpr_target;
    int *constexpr_length;
    int constexpr_capacity;

    wasm_valtype blocktype_params[WAST_MAX_PARAMS];
    int blocktype_param_count;
    wasm_valtype blocktype_results[WAST_MAX_RESULTS];
    int blocktype_result_count;
    int blocktype_explicit;
    int typeuse_field_stage;
    int signature_seen_result;
} wat_context;

#endif
