#include "wat/builder.h"
#include "wat/context.h"

#include <stdlib.h>
#include <string.h>

static int hex_digit(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (unsigned char)(c | 0x20u);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static int append_byte(uint8_t *destination, size_t *length,
                       size_t capacity, uint8_t byte) {
    if (*length >= capacity) return 0;
    destination[(*length)++] = byte;
    return 1;
}

static int append_utf8(uint8_t *destination, size_t *length,
                       size_t capacity, uint32_t codepoint) {
    if (codepoint <= 0x7fu)
        return append_byte(destination, length, capacity, (uint8_t)codepoint);
    if (codepoint <= 0x7ffu)
        return append_byte(destination, length, capacity,
                           (uint8_t)(0xc0u | (codepoint >> 6))) &&
               append_byte(destination, length, capacity,
                           (uint8_t)(0x80u | (codepoint & 0x3fu)));
    if (codepoint <= 0xffffu)
        return append_byte(destination, length, capacity,
                           (uint8_t)(0xe0u | (codepoint >> 12))) &&
               append_byte(destination, length, capacity,
                           (uint8_t)(0x80u | ((codepoint >> 6) & 0x3fu))) &&
               append_byte(destination, length, capacity,
                           (uint8_t)(0x80u | (codepoint & 0x3fu)));
    if (codepoint > 0x10ffffu) return 0;
    return append_byte(destination, length, capacity,
                       (uint8_t)(0xf0u | (codepoint >> 18))) &&
           append_byte(destination, length, capacity,
                       (uint8_t)(0x80u | ((codepoint >> 12) & 0x3fu))) &&
           append_byte(destination, length, capacity,
                       (uint8_t)(0x80u | ((codepoint >> 6) & 0x3fu))) &&
           append_byte(destination, length, capacity,
                       (uint8_t)(0x80u | (codepoint & 0x3fu)));
}

static int decode_string(const char *source, size_t source_length,
                         uint8_t *destination, size_t *length,
                         size_t capacity) {
    for (size_t i = 0; i < source_length; i++) {
        unsigned char c = (unsigned char)source[i];
        if (c != '\\') {
            if (!append_byte(destination, length, capacity, c)) return 0;
            continue;
        }
        if (++i >= source_length) return 0;
        c = (unsigned char)source[i];
        int high = hex_digit(c);
        int low = i + 1 < source_length ?
                  hex_digit((unsigned char)source[i + 1]) : -1;
        if (high >= 0 && low >= 0) {
            if (!append_byte(destination, length, capacity,
                             (uint8_t)((high << 4) | low))) return 0;
            i++;
        } else if (c == 'u' && i + 1 < source_length &&
                   source[i + 1] == '{') {
            uint32_t codepoint = 0;
            int digits = 0;
            i += 2;
            while (i < source_length && source[i] != '}') {
                int digit = hex_digit((unsigned char)source[i++]);
                if (digit < 0 || codepoint > 0x10ffffu / 16u) return 0;
                codepoint = codepoint * 16u + (uint32_t)digit;
                digits++;
            }
            if (!digits || i >= source_length || source[i] != '}' ||
                !append_utf8(destination, length, capacity, codepoint))
                return 0;
        } else {
            if (c == 't') c = '\t';
            else if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            if (!append_byte(destination, length, capacity, c)) return 0;
        }
    }
    return 1;
}

wat_context *wast_builder_context_create(void) {
    /* Most of this object is bounded scratch storage.  Its count fields guard
     * every array read, so zero only live state instead of clearing roughly
     * five megabytes for every WAST command/file parse. */
    wat_context *context = (wat_context *)malloc(sizeof(*context));
    if (!context) return NULL;
    memset(&context->lex, 0, sizeof(context->lex));
    context->lex.line = 1;
    context->lex.column = 1;
    context->lex.byte_offset = 0;
    context->literal_scratch[0] = '\0';
    context->lexer_error[0] = '\0';
    context->skip_annotation_depth = 0;
    context->skip_comment_depth = 0;
    context->skip_in_string = 0;
    context->skip_line_comment = 0;
    context->skip_return_paren = 0;
    context->paren_line = 1;
    context->paren_column = 1;
    context->inline_module = 0;
    context->inline_token_emitted = 0;
    context->strict_wat_mode = 0;
    context->command_scan = 0;
    context->command_scan_inline = 0;
    context->command_scan_started = 0;
    context->command_scan_finished = 0;
    context->command_scan_depth = 0;
    context->command_start_offset = 0;
    context->command_end_offset = 0;
    context->command_start_line = 1;
    context->numeric_kind = 0;
    context->numeric_bits = 0;
    context->numeric_remaining = 0;
    context->v128_const_pending = 0;
    context->raw_payload = (wast_raw_module){WAST_RAW_NONE, NULL, 0};
    context->raw_payload_capacity = 0;
    memset(&context->cur_func, 0, sizeof(context->cur_func));
    context->in_func = 0;
    context->cur_group = 0;
    context->cur_func_index = 0;
    context->label_depth = 0;
    context->local_name_count = 0;
    context->func_name_count = 0;
    context->global_name_count = 0;
    context->type_name_count = 0;
    context->in_rec_group = 0;
    context->parsing_type_definition = 0;
    context->rec_group_start = 0;
    context->table_name_count = 0;
    context->memory_name_count = 0;
    context->data_name_count = 0;
    context->elem_name_count = 0;
    context->tag_name_count = 0;
    memset(&context->cur_tag, 0, sizeof(context->cur_tag));
    context->func_fixup_count = 0;
    context->type_fixup_count = 0;
    context->code_fixup_count = 0;
    context->meta_fixup_count = 0;
    context->rec_fixup_count = 0;
    memset(&context->cur_assert, 0, sizeof(context->cur_assert));
    context->in_assert = 0;
    context->invoke_name[0] = '\0';
    context->module_assert_action = 0;
    context->brtable_count = 0;
    context->try_catch_count = 0;
    context->select_result_count = 0;
    context->lane_imm_count = 0;
    context->inline_param_count = 0;
    context->inline_result_count = 0;
    context->import_module[0] = '\0';
    context->import_name[0] = '\0';
    context->export_kind = 0;
    context->export_index = 0;
    context->instance_args[0][0] = '\0';
    context->instance_args[1][0] = '\0';
    context->instance_arg_count = 0;
    memset(&context->cur_global, 0, sizeof(context->cur_global));
    memset(&context->cur_type, 0, sizeof(context->cur_type));
    memset(&context->cur_data, 0, sizeof(context->cur_data));
    memset(&context->cur_elem, 0, sizeof(context->cur_elem));
    context->constexpr_target = NULL;
    context->constexpr_length = NULL;
    context->constexpr_capacity = 0;
    context->blocktype_param_count = 0;
    context->blocktype_result_count = 0;
    context->blocktype_explicit = -1;
    context->typeuse_field_stage = 0;
    context->signature_seen_result = 0;
    return context;
}

void wast_builder_context_destroy(wat_context *context) {
    if (!context) return;
    for (size_t i = 0; i < context->lex.string_count; i++)
        free(context->lex.strings[i].data);
    free(context->lex.strings);
    free(context->raw_payload.bytes);
    free(context);
}

char *wast_builder_retain_string(wat_context *context,
                                 const char *source, size_t length) {
    char *value = (char *)malloc(length + 1);
    if (!value) return NULL;
    memcpy(value, source, length);
    value[length] = '\0';
    if (context->lex.string_count == context->lex.string_capacity) {
        size_t capacity = context->lex.string_capacity ?
                          context->lex.string_capacity * 2 : 64;
        wast_owned_string *strings = (wast_owned_string *)realloc(
            context->lex.strings, capacity * sizeof(*strings));
        if (!strings) {
            free(value);
            return NULL;
        }
        context->lex.strings = strings;
        context->lex.string_capacity = capacity;
    }
    wast_owned_string *owned =
        &context->lex.strings[context->lex.string_count++];
    owned->data = value;
    owned->length = length;
    return value;
}

size_t wast_builder_string_length(const wat_context *context,
                                  const char *string) {
    for (size_t i = 0; i < context->lex.string_count; i++)
        if (context->lex.strings[i].data == string)
            return context->lex.strings[i].length;
    return strlen(string);
}

void wast_builder_begin_raw(wat_context *context, wast_raw_module_kind kind) {
    free(context->raw_payload.bytes);
    context->raw_payload = (wast_raw_module){kind, NULL, 0};
    context->raw_payload_capacity = 0;
}

static int reserve_raw(wat_context *context, size_t additional) {
    if (additional > SIZE_MAX - context->raw_payload.length) return 0;
    size_t required = context->raw_payload.length + additional;
    if (required <= context->raw_payload_capacity) return 1;
    size_t capacity = context->raw_payload_capacity ?
                      context->raw_payload_capacity : 64;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) { capacity = required; break; }
        capacity *= 2;
    }
    uint8_t *bytes = (uint8_t *)realloc(context->raw_payload.bytes, capacity);
    if (!bytes) return 0;
    context->raw_payload.bytes = bytes;
    context->raw_payload_capacity = capacity;
    return 1;
}

int wast_builder_append_raw_string(wat_context *context, const char *string) {
    if (context->raw_payload.kind == WAST_RAW_NONE) return 1;
    size_t source_length = wast_builder_string_length(context, string);
    if (source_length > (SIZE_MAX - 1u) / 4u) return 0;
    if (!reserve_raw(context, source_length * 4u + 1u)) return 0;
    return decode_string(string, source_length, context->raw_payload.bytes,
                         &context->raw_payload.length,
                         context->raw_payload_capacity);
}

int wast_builder_take_raw(wat_context *context, wast_raw_module_kind kind,
                          wast_raw_module *raw) {
    if (context->raw_payload.kind != kind) return 0;
    *raw = context->raw_payload;
    context->raw_payload = (wast_raw_module){WAST_RAW_NONE, NULL, 0};
    context->raw_payload_capacity = 0;
    return 1;
}

int wast_builder_decode_string(wat_context *context, const char *string,
                               uint8_t *destination, size_t *length,
                               size_t capacity) {
    return decode_string(string, wast_builder_string_length(context, string),
                         destination, length, capacity);
}
