#include "waste_tail.h"

#include <stdbool.h>
#ifndef WASTE_FREESTANDING
#include <stdlib.h>
#include <string.h>
#else
void *calloc(size_t count, size_t size);
void free(void *pointer);
void *memcpy(void *destination, const void *source, size_t count);
int memcmp(const void *left, const void *right, size_t count);
int strcmp(const char *left, const char *right);
#endif

enum {
  WASTE_MAX_TYPES = 64,
  WASTE_MAX_FUNCTIONS = 128,
  WASTE_MAX_EXPORTS = 128,
  WASTE_MAX_EXPORT_NAME = 63,
  WASTE_MAX_CONTROL_DEPTH = 64,
  WASTE_MAX_VALUES = 64
};

typedef struct {
  const uint8_t *start;
  const uint8_t *cursor;
  const uint8_t *end;
} waste_reader;

typedef enum {
  OP_LOCAL_GET,
  OP_I64_CONST,
  OP_I64_EQZ,
  OP_I64_SUB,
  OP_IF,
  OP_ELSE,
  OP_END,
  OP_RETURN_CALL,
  OP_REF_FUNC,
  OP_RETURN_CALL_REF
} waste_opcode;

typedef struct {
  waste_opcode opcode;
  uint32_t immediate;
  int64_t constant;
} waste_instruction;

typedef struct {
  uint8_t parameter_count;
  uint8_t result_count;
  uint8_t parameter_type;
  uint8_t result_type;
} waste_function_type;

typedef struct {
  uint32_t type_index;
  waste_instruction *code;
  uint32_t code_size;
} waste_function;

typedef struct {
  char name[WASTE_MAX_EXPORT_NAME + 1];
  uint32_t function_index;
} waste_export;

struct waste_module {
  waste_function_type *types;
  uint32_t type_count;
  waste_function *functions;
  uint32_t function_count;
  waste_export *exports;
  uint32_t export_count;
};

typedef struct {
  uint32_t if_pc;
  uint32_t else_pc;
  bool has_else;
} waste_control;

typedef enum {
  VALUE_I32,
  VALUE_I64,
  VALUE_FUNCREF
} waste_value_type;

typedef struct {
  waste_value_type type;
  union {
    int32_t i32;
    int64_t i64;
    uint32_t function_index;
  } value;
} waste_value;

static waste_status fail(
  waste_error *error,
  waste_status status,
  size_t offset,
  const char *message
) {
  if (error != NULL) {
    size_t index = 0;
    error->status = status;
    error->offset = offset;
    while (index + 1 < sizeof(error->message) && message[index] != '\0') {
      error->message[index] = message[index];
      ++index;
    }
    error->message[index] = '\0';
  }
  return status;
}

static size_t reader_offset(const waste_reader *reader) {
  return (size_t)(reader->cursor - reader->start);
}

static bool read_u8(waste_reader *reader, uint8_t *value) {
  if (reader->cursor == reader->end) {
    return false;
  }
  *value = *reader->cursor++;
  return true;
}

static bool read_bytes(waste_reader *reader, size_t count, const uint8_t **bytes) {
  if (count > (size_t)(reader->end - reader->cursor)) {
    return false;
  }
  *bytes = reader->cursor;
  reader->cursor += count;
  return true;
}

static bool read_u32(waste_reader *reader, uint32_t *value) {
  uint32_t result = 0;
  unsigned shift = 0;
  for (unsigned index = 0; index < 5; ++index) {
    uint8_t byte;
    if (!read_u8(reader, &byte)) {
      return false;
    }
    if (index == 4 && (byte & 0xf0u) != 0) {
      return false;
    }
    result |= (uint32_t)(byte & 0x7fu) << shift;
    if ((byte & 0x80u) == 0) {
      *value = result;
      return true;
    }
    shift += 7;
  }
  return false;
}

static bool read_i64(waste_reader *reader, int64_t *value) {
  uint64_t result = 0;
  unsigned shift = 0;
  uint8_t byte = 0;
  for (unsigned index = 0; index < 10; ++index) {
    if (!read_u8(reader, &byte)) {
      return false;
    }
    if (index == 9 && (byte & 0x7eu) != 0 && (byte & 0x7fu) != 0x7fu) {
      return false;
    }
    result |= (uint64_t)(byte & 0x7fu) << shift;
    shift += 7;
    if ((byte & 0x80u) == 0) {
      if (shift < 64 && (byte & 0x40u) != 0) {
        result |= UINT64_MAX << shift;
      }
      *value = (int64_t)result;
      return true;
    }
  }
  return false;
}

static waste_status parse_types(
  waste_module *module,
  waste_reader *section,
  waste_error *error
) {
  uint32_t count;
  if (!read_u32(section, &count)) {
    return fail(error, WASTE_ERROR_FORMAT, reader_offset(section), "invalid type count");
  }
  if (count > WASTE_MAX_TYPES) {
    return fail(error, WASTE_ERROR_LIMIT, reader_offset(section), "too many function types");
  }
  module->types = calloc(count, sizeof(*module->types));
  if (count != 0 && module->types == NULL) {
    return fail(error, WASTE_ERROR_LIMIT, reader_offset(section), "type allocation failed");
  }
  module->type_count = count;
  for (uint32_t index = 0; index < count; ++index) {
    uint8_t form;
    uint32_t parameters;
    uint32_t results;
    uint8_t parameter_type;
    uint8_t result_type;
    if (!read_u8(section, &form) || form != 0x60u ||
        !read_u32(section, &parameters) || parameters != 1 ||
        !read_u8(section, &parameter_type) || parameter_type != 0x7eu ||
        !read_u32(section, &results) || results != 1 ||
        !read_u8(section, &result_type) || result_type != 0x7eu) {
      return fail(error, WASTE_ERROR_UNSUPPORTED, reader_offset(section),
        "proof engine requires function type (param i64) (result i64)");
    }
    module->types[index] = (waste_function_type){1, 1, parameter_type, result_type};
  }
  return WASTE_OK;
}

static waste_status parse_functions(
  waste_module *module,
  waste_reader *section,
  waste_error *error
) {
  uint32_t count;
  if (!read_u32(section, &count)) {
    return fail(error, WASTE_ERROR_FORMAT, reader_offset(section), "invalid function count");
  }
  if (count > WASTE_MAX_FUNCTIONS) {
    return fail(error, WASTE_ERROR_LIMIT, reader_offset(section), "too many functions");
  }
  module->functions = calloc(count, sizeof(*module->functions));
  if (count != 0 && module->functions == NULL) {
    return fail(error, WASTE_ERROR_LIMIT, reader_offset(section), "function allocation failed");
  }
  module->function_count = count;
  for (uint32_t index = 0; index < count; ++index) {
    uint32_t type_index;
    if (!read_u32(section, &type_index) || type_index >= module->type_count) {
      return fail(error, WASTE_ERROR_FORMAT, reader_offset(section), "invalid function type index");
    }
    module->functions[index].type_index = type_index;
  }
  return WASTE_OK;
}

static waste_status parse_exports(
  waste_module *module,
  waste_reader *section,
  waste_error *error
) {
  uint32_t count;
  if (!read_u32(section, &count)) {
    return fail(error, WASTE_ERROR_FORMAT, reader_offset(section), "invalid export count");
  }
  if (count > WASTE_MAX_EXPORTS) {
    return fail(error, WASTE_ERROR_LIMIT, reader_offset(section), "too many exports");
  }
  module->exports = calloc(count, sizeof(*module->exports));
  if (count != 0 && module->exports == NULL) {
    return fail(error, WASTE_ERROR_LIMIT, reader_offset(section), "export allocation failed");
  }
  module->export_count = count;
  for (uint32_t index = 0; index < count; ++index) {
    uint32_t name_size;
    uint32_t function_index;
    uint8_t kind;
    const uint8_t *name;
    if (!read_u32(section, &name_size) || name_size > WASTE_MAX_EXPORT_NAME ||
        !read_bytes(section, name_size, &name) || !read_u8(section, &kind) ||
        kind != 0 || !read_u32(section, &function_index) ||
        function_index >= module->function_count) {
      return fail(error, WASTE_ERROR_UNSUPPORTED, reader_offset(section),
        "invalid or unsupported function export");
    }
    memcpy(module->exports[index].name, name, name_size);
    module->exports[index].name[name_size] = '\0';
    module->exports[index].function_index = function_index;
  }
  return WASTE_OK;
}

static waste_status append_instruction(
  waste_instruction *code,
  uint32_t capacity,
  uint32_t *size,
  waste_instruction instruction,
  waste_reader *body,
  waste_error *error
) {
  if (*size >= capacity) {
    return fail(error, WASTE_ERROR_LIMIT, reader_offset(body), "instruction limit exceeded");
  }
  code[(*size)++] = instruction;
  return WASTE_OK;
}

static waste_status parse_body(
  waste_module *module,
  waste_function *function,
  waste_reader *body,
  waste_error *error
) {
  uint32_t local_groups;
  uint32_t capacity = (uint32_t)(body->end - body->cursor);
  uint32_t code_size = 0;
  uint32_t control_size = 0;
  bool finished = false;
  waste_control controls[WASTE_MAX_CONTROL_DEPTH];
  waste_instruction *code;

  if (!read_u32(body, &local_groups) || local_groups != 0) {
    return fail(error, WASTE_ERROR_UNSUPPORTED, reader_offset(body),
      "proof engine does not support additional locals");
  }
  code = calloc(capacity == 0 ? 1u : capacity, sizeof(*code));
  if (code == NULL) {
    return fail(error, WASTE_ERROR_LIMIT, reader_offset(body), "code allocation failed");
  }

  while (body->cursor < body->end && !finished) {
    uint8_t byte;
    waste_instruction instruction = {0};
    waste_status status;
    if (!read_u8(body, &byte)) {
      free(code);
      return fail(error, WASTE_ERROR_FORMAT, reader_offset(body), "truncated instruction");
    }
    switch (byte) {
      case 0x20: /* local.get */
        instruction.opcode = OP_LOCAL_GET;
        if (!read_u32(body, &instruction.immediate) || instruction.immediate != 0) {
          free(code);
          return fail(error, WASTE_ERROR_UNSUPPORTED, reader_offset(body),
            "only local.get 0 is supported");
        }
        break;
      case 0x42: /* i64.const */
        instruction.opcode = OP_I64_CONST;
        if (!read_i64(body, &instruction.constant)) {
          free(code);
          return fail(error, WASTE_ERROR_FORMAT, reader_offset(body), "invalid i64 constant");
        }
        break;
      case 0x50: instruction.opcode = OP_I64_EQZ; break;
      case 0x7d: instruction.opcode = OP_I64_SUB; break;
      case 0x04: { /* if */
        uint8_t block_type;
        instruction.opcode = OP_IF;
        instruction.immediate = UINT32_MAX;
        if (!read_u8(body, &block_type) || block_type != 0x7eu) {
          free(code);
          return fail(error, WASTE_ERROR_UNSUPPORTED, reader_offset(body),
            "only if (result i64) is supported");
        }
        if (control_size == WASTE_MAX_CONTROL_DEPTH) {
          free(code);
          return fail(error, WASTE_ERROR_LIMIT, reader_offset(body), "control nesting limit exceeded");
        }
        controls[control_size++] = (waste_control){code_size, UINT32_MAX, false};
        break;
      }
      case 0x05: /* else */
        instruction.opcode = OP_ELSE;
        instruction.immediate = UINT32_MAX;
        if (control_size == 0 || controls[control_size - 1].has_else) {
          free(code);
          return fail(error, WASTE_ERROR_FORMAT, reader_offset(body), "unexpected else");
        }
        controls[control_size - 1].has_else = true;
        controls[control_size - 1].else_pc = code_size;
        code[controls[control_size - 1].if_pc].immediate = code_size + 1;
        break;
      case 0x0b: /* end */
        instruction.opcode = OP_END;
        if (control_size == 0) {
          finished = true;
        } else {
          waste_control control = controls[--control_size];
          if (control.has_else) {
            code[control.else_pc].immediate = code_size + 1;
          } else {
            code[control.if_pc].immediate = code_size + 1;
          }
        }
        break;
      case 0x12: /* return_call */
        instruction.opcode = OP_RETURN_CALL;
        if (!read_u32(body, &instruction.immediate) ||
            instruction.immediate >= module->function_count) {
          free(code);
          return fail(error, WASTE_ERROR_FORMAT, reader_offset(body), "invalid return_call target");
        }
        break;
      case 0xd2: /* ref.func */
        instruction.opcode = OP_REF_FUNC;
        if (!read_u32(body, &instruction.immediate) ||
            instruction.immediate >= module->function_count) {
          free(code);
          return fail(error, WASTE_ERROR_FORMAT, reader_offset(body), "invalid ref.func target");
        }
        break;
      case 0x15: /* return_call_ref */
        instruction.opcode = OP_RETURN_CALL_REF;
        if (!read_u32(body, &instruction.immediate) ||
            instruction.immediate >= module->type_count) {
          free(code);
          return fail(error, WASTE_ERROR_FORMAT, reader_offset(body),
            "invalid return_call_ref type");
        }
        break;
      default:
        free(code);
        return fail(error, WASTE_ERROR_UNSUPPORTED, reader_offset(body) - 1,
          "unsupported instruction in tail-call proof module");
    }
    status = append_instruction(code, capacity, &code_size, instruction, body, error);
    if (status != WASTE_OK) {
      free(code);
      return status;
    }
  }
  if (!finished || control_size != 0 || body->cursor != body->end) {
    free(code);
    return fail(error, WASTE_ERROR_FORMAT, reader_offset(body), "malformed function expression");
  }
  function->code = code;
  function->code_size = code_size;
  return WASTE_OK;
}

static waste_status parse_code(
  waste_module *module,
  waste_reader *section,
  waste_error *error
) {
  uint32_t count;
  if (!read_u32(section, &count) || count != module->function_count) {
    return fail(error, WASTE_ERROR_FORMAT, reader_offset(section), "code/function count mismatch");
  }
  for (uint32_t index = 0; index < count; ++index) {
    uint32_t body_size;
    const uint8_t *body_bytes;
    waste_reader body;
    waste_status status;
    if (!read_u32(section, &body_size) || !read_bytes(section, body_size, &body_bytes)) {
      return fail(error, WASTE_ERROR_FORMAT, reader_offset(section), "truncated function body");
    }
    body = (waste_reader){section->start, body_bytes, body_bytes + body_size};
    status = parse_body(module, &module->functions[index], &body, error);
    if (status != WASTE_OK) {
      return status;
    }
  }
  return WASTE_OK;
}

static waste_status parse_module(
  waste_module *module,
  waste_reader *reader,
  waste_error *error
) {
  static const uint8_t header[8] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
  const uint8_t *actual_header;
  uint8_t last_section = 0;
  bool seen[13] = {false};
  if (!read_bytes(reader, sizeof(header), &actual_header) ||
      memcmp(actual_header, header, sizeof(header)) != 0) {
    return fail(error, WASTE_ERROR_FORMAT, 0, "invalid WebAssembly header");
  }
  while (reader->cursor < reader->end) {
    uint8_t section_id;
    uint32_t section_size;
    const uint8_t *section_bytes;
    waste_reader section;
    waste_status status = WASTE_OK;
    if (!read_u8(reader, &section_id) || !read_u32(reader, &section_size) ||
        !read_bytes(reader, section_size, &section_bytes)) {
      return fail(error, WASTE_ERROR_FORMAT, reader_offset(reader), "truncated section");
    }
    if (section_id > 12) {
      return fail(error, WASTE_ERROR_UNSUPPORTED, reader_offset(reader), "unknown section id");
    }
    if (section_id != 0) {
      if (seen[section_id] || section_id < last_section) {
        return fail(error, WASTE_ERROR_FORMAT, reader_offset(reader), "duplicate or out-of-order section");
      }
      seen[section_id] = true;
      last_section = section_id;
    }
    section = (waste_reader){reader->start, section_bytes, section_bytes + section_size};
    switch (section_id) {
      case 0: /* custom */
      case 9: /* declarative elem needed by ref.func validation */
        section.cursor = section.end;
        break;
      case 1: status = parse_types(module, &section, error); break;
      case 3: status = parse_functions(module, &section, error); break;
      case 7: status = parse_exports(module, &section, error); break;
      case 10: status = parse_code(module, &section, error); break;
      default:
        return fail(error, WASTE_ERROR_UNSUPPORTED, reader_offset(reader),
          "section is outside the tail-call proof subset");
    }
    if (status != WASTE_OK) {
      return status;
    }
    if (section.cursor != section.end) {
      return fail(error, WASTE_ERROR_FORMAT, reader_offset(&section), "trailing section data");
    }
  }
  if (!seen[1] || !seen[3] || !seen[7] || !seen[10]) {
    return fail(error, WASTE_ERROR_FORMAT, reader_offset(reader), "required section is missing");
  }
  return WASTE_OK;
}

waste_status waste_module_load(
  const uint8_t *bytes,
  size_t size,
  waste_module **module_out,
  waste_error *error
) {
  waste_module *module;
  waste_reader reader;
  waste_status status;
  if (module_out == NULL || bytes == NULL) {
    return fail(error, WASTE_ERROR_FORMAT, 0, "null module input");
  }
  *module_out = NULL;
  module = calloc(1, sizeof(*module));
  if (module == NULL) {
    return fail(error, WASTE_ERROR_LIMIT, 0, "module allocation failed");
  }
  reader = (waste_reader){bytes, bytes, bytes + size};
  status = parse_module(module, &reader, error);
  if (status != WASTE_OK) {
    waste_module_free(module);
    return status;
  }
  *module_out = module;
  return WASTE_OK;
}

void waste_module_free(waste_module *module) {
  if (module == NULL) {
    return;
  }
  for (uint32_t index = 0; index < module->function_count; ++index) {
    free(module->functions[index].code);
  }
  free(module->types);
  free(module->functions);
  free(module->exports);
  free(module);
}

waste_status waste_module_find_export(
  const waste_module *module,
  const char *name,
  uint32_t *function_index,
  waste_error *error
) {
  if (module == NULL || name == NULL || function_index == NULL) {
    return fail(error, WASTE_ERROR_FORMAT, 0, "invalid export lookup");
  }
  for (uint32_t index = 0; index < module->export_count; ++index) {
    if (strcmp(module->exports[index].name, name) == 0) {
      *function_index = module->exports[index].function_index;
      return WASTE_OK;
    }
  }
  return fail(error, WASTE_ERROR_NOT_FOUND, 0, "function export not found");
}

static waste_status push(
  waste_value *values,
  uint32_t *size,
  waste_value value,
  waste_error *error
) {
  if (*size == WASTE_MAX_VALUES) {
    return fail(error, WASTE_ERROR_LIMIT, 0, "value stack limit exceeded");
  }
  values[(*size)++] = value;
  return WASTE_OK;
}

static waste_status pop(
  waste_value *values,
  uint32_t *size,
  waste_value_type type,
  waste_value *value,
  waste_error *error
) {
  if (*size == 0 || values[*size - 1].type != type) {
    return fail(error, WASTE_ERROR_TRAP, 0, "value stack type mismatch");
  }
  *value = values[--(*size)];
  return WASTE_OK;
}

static bool compatible_function(const waste_module *module, uint32_t function_index) {
  const waste_function *function;
  const waste_function_type *type;
  if (function_index >= module->function_count) {
    return false;
  }
  function = &module->functions[function_index];
  if (function->type_index >= module->type_count) {
    return false;
  }
  type = &module->types[function->type_index];
  return type->parameter_count == 1 && type->result_count == 1 &&
    type->parameter_type == 0x7e && type->result_type == 0x7e;
}

waste_status waste_run_i64(
  const waste_module *module,
  uint32_t function_index,
  int64_t argument,
  uint64_t fuel,
  int64_t *result,
  waste_run_stats *stats,
  waste_error *error
) {
  waste_value values[WASTE_MAX_VALUES];
  uint32_t value_size = 0;
  uint32_t pc = 0;
  int64_t local = argument;
  waste_run_stats local_stats = {0, 0, 1, 0};

  if (module == NULL || result == NULL || !compatible_function(module, function_index)) {
    return fail(error, WASTE_ERROR_UNSUPPORTED, 0, "incompatible entry function");
  }
  while (true) {
    const waste_function *function = &module->functions[function_index];
    waste_instruction instruction;
    waste_value left;
    waste_value right;
    waste_status status;
    uint32_t target;

    if (pc >= function->code_size) {
      return fail(error, WASTE_ERROR_TRAP, pc, "program counter outside function");
    }
    if (fuel == 0) {
      return fail(error, WASTE_ERROR_FUEL, pc, "execution fuel exhausted");
    }
    --fuel;
    ++local_stats.instructions;
    instruction = function->code[pc++];
    switch (instruction.opcode) {
      case OP_LOCAL_GET:
        status = push(values, &value_size,
          (waste_value){VALUE_I64, {.i64 = local}}, error);
        if (status != WASTE_OK) return status;
        break;
      case OP_I64_CONST:
        status = push(values, &value_size,
          (waste_value){VALUE_I64, {.i64 = instruction.constant}}, error);
        if (status != WASTE_OK) return status;
        break;
      case OP_I64_EQZ:
        status = pop(values, &value_size, VALUE_I64, &left, error);
        if (status != WASTE_OK) return status;
        status = push(values, &value_size,
          (waste_value){VALUE_I32, {.i32 = left.value.i64 == 0}}, error);
        if (status != WASTE_OK) return status;
        break;
      case OP_I64_SUB:
        status = pop(values, &value_size, VALUE_I64, &right, error);
        if (status != WASTE_OK) return status;
        status = pop(values, &value_size, VALUE_I64, &left, error);
        if (status != WASTE_OK) return status;
        status = push(values, &value_size,
          (waste_value){VALUE_I64, {
            .i64 = (int64_t)((uint64_t)left.value.i64 - (uint64_t)right.value.i64)
          }}, error);
        if (status != WASTE_OK) return status;
        break;
      case OP_IF:
        status = pop(values, &value_size, VALUE_I32, &left, error);
        if (status != WASTE_OK) return status;
        if (left.value.i32 == 0) pc = instruction.immediate;
        break;
      case OP_ELSE:
        pc = instruction.immediate;
        break;
      case OP_END:
        if (pc == function->code_size) {
          status = pop(values, &value_size, VALUE_I64, &left, error);
          if (status != WASTE_OK || value_size != 0) {
            return fail(error, WASTE_ERROR_TRAP, pc, "invalid function result stack");
          }
          *result = left.value.i64;
          if (stats != NULL) *stats = local_stats;
          return WASTE_OK;
        }
        break;
      case OP_RETURN_CALL:
        target = instruction.immediate;
        status = pop(values, &value_size, VALUE_I64, &left, error);
        if (status != WASTE_OK) return status;
        if (!compatible_function(module, target)) {
          return fail(error, WASTE_ERROR_TRAP, pc, "return_call type mismatch");
        }
        local = left.value.i64;
        value_size = 0;
        function_index = target;
        pc = 0;
        ++local_stats.tail_calls;
        break;
      case OP_REF_FUNC:
        status = push(values, &value_size,
          (waste_value){VALUE_FUNCREF, {.function_index = instruction.immediate}}, error);
        if (status != WASTE_OK) return status;
        break;
      case OP_RETURN_CALL_REF:
        status = pop(values, &value_size, VALUE_FUNCREF, &right, error);
        if (status != WASTE_OK) return status;
        status = pop(values, &value_size, VALUE_I64, &left, error);
        if (status != WASTE_OK) return status;
        target = right.value.function_index;
        if (!compatible_function(module, target) ||
            module->functions[target].type_index != instruction.immediate) {
          return fail(error, WASTE_ERROR_TRAP, pc, "return_call_ref type mismatch");
        }
        local = left.value.i64;
        value_size = 0;
        function_index = target;
        pc = 0;
        ++local_stats.tail_calls;
        break;
    }
  }
}

const char *waste_status_name(waste_status status) {
  switch (status) {
    case WASTE_OK: return "ok";
    case WASTE_ERROR_FORMAT: return "format";
    case WASTE_ERROR_UNSUPPORTED: return "unsupported";
    case WASTE_ERROR_LIMIT: return "limit";
    case WASTE_ERROR_NOT_FOUND: return "not-found";
    case WASTE_ERROR_TRAP: return "trap";
    case WASTE_ERROR_FUEL: return "fuel";
  }
  return "unknown";
}
