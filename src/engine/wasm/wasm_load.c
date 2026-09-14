#include "runtime/engine_internal.h"
#include "wasm/wasm_opcode.h"
#include "wasm/wasm_decode.h"
#include "wasm/wasm_reader.h"
#include "wasm/wasm_validate.h"
#include "runtime/instantiate.h"
#include "runtime/runtime_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>


static int valid_utf8(const uint8_t *bytes, size_t length) {
    size_t i = 0;
    while (i < length) {
        uint8_t first = bytes[i++];
        if (first <= 0x7f) continue;
        if (first >= 0xc2 && first <= 0xdf) {
            if (i >= length || bytes[i] < 0x80 || bytes[i] > 0xbf) return 0;
            i++;
        } else if (first >= 0xe0 && first <= 0xef) {
            if (i + 1 >= length) return 0;
            uint8_t second = bytes[i], third = bytes[i + 1];
            if (third < 0x80 || third > 0xbf ||
                (first == 0xe0 && (second < 0xa0 || second > 0xbf)) ||
                (first == 0xed && (second < 0x80 || second > 0x9f)) ||
                (first != 0xe0 && first != 0xed &&
                 (second < 0x80 || second > 0xbf))) return 0;
            i += 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            if (i + 2 >= length) return 0;
            uint8_t second = bytes[i], third = bytes[i + 1], fourth = bytes[i + 2];
            if (third < 0x80 || third > 0xbf || fourth < 0x80 || fourth > 0xbf ||
                (first == 0xf0 && (second < 0x90 || second > 0xbf)) ||
                (first == 0xf4 && (second < 0x80 || second > 0x8f)) ||
                (first != 0xf0 && first != 0xf4 &&
                 (second < 0x80 || second > 0xbf))) return 0;
            i += 3;
        } else return 0;
    }
    return 1;
}



static const exec_host_import *find_host_import(const exec_imports *imports,
                                                 const char *module, const char *name) {
    if (!imports) return NULL;
    for (size_t i = 0; i < imports->function_count; i++)
        if (strcmp(imports->functions[i].module, module) == 0 &&
            strcmp(imports->functions[i].name, name) == 0) return &imports->functions[i];
    return NULL;
}

static exec_global *find_global_import(const exec_imports *imports, const char *module, const char *name) {
    if (!imports) return NULL;
    for (size_t i=0;i<imports->global_count;i++)
        if (strcmp(imports->globals[i].module,module)==0 && strcmp(imports->globals[i].name,name)==0)
            return imports->globals[i].global;
    return NULL;
}
static exec_memory *find_memory_import(const exec_imports *imports, const char *module, const char *name) {
    if (!imports) return NULL;
    for (size_t i=0;i<imports->memory_count;i++)
        if (strcmp(imports->memories[i].module,module)==0 && strcmp(imports->memories[i].name,name)==0)
            return imports->memories[i].memory;
    return NULL;
}
static exec_table *find_table_import(const exec_imports *imports, const char *module, const char *name) {
    if (!imports) return NULL;
    for (size_t i=0;i<imports->table_count;i++)
        if (strcmp(imports->tables[i].module,module)==0 && strcmp(imports->tables[i].name,name)==0)
            return imports->tables[i].table;
    return NULL;
}
static exec_tag *find_tag_import(const exec_imports *imports, const char *module,
                                 const char *name) {
    if(!imports)return NULL;
    for(size_t i=0;i<imports->tag_count;i++)
        if(strcmp(imports->tags[i].module,module)==0&&
           strcmp(imports->tags[i].name,name)==0)return imports->tags[i].tag;
    return NULL;
}

/* ---- Error helper ---- */

exec_status exec_fail(exec_error *error, exec_status status, const char *msg) {
    if (error) {
        error->status = status;
        snprintf(error->message, sizeof(error->message), "%s", msg);
    }
    return status;
}

/* ---- Type section ---- */

static int parse_storage_type(wasm_reader *sec, wasm_valtype *type,
                              uint8_t *packed) {
    uint8_t first;
    if (!wasm_reader_read_u8(sec, &first)) return 0;
    if (first == 0x78 || first == 0x77) {
        *type = WASM_VALTYPE_I32;
        *packed = first == 0x78 ? 1 : 2;
        return 1;
    }
    sec->cursor--;
    *packed = 0;
    return wasm_decode_valtype(sec, type);
}

static exec_status parse_composite_type(waste_exec_engine *eng,
                                        wasm_reader *sec, uint8_t form,
                                        uint32_t index,
                                        uint32_t group_start,
                                        uint32_t group_size,
                                        exec_error *err) {
    exec_func_type *type = &eng->types[index];
    type->rec_group_start = group_start;
    type->rec_group_size = group_size;
    type->supertype = -1;
    type->is_final = 1;
    if (form == 0x4f || form == 0x50) {
        uint8_t wrapper = form;
        uint32_t super_count, supertype = 0;
        if (!wasm_reader_read_u32(sec, &super_count) || super_count > 1 ||
            (super_count == 1 && !wasm_reader_read_u32(sec, &supertype)) ||
            !wasm_reader_read_u8(sec, &form))
            return exec_fail(err, EXEC_ERROR_FORMAT,
                             "invalid subtype definition");
        type->is_final = (uint8_t)(wrapper == 0x4f);
        if (super_count == 1) type->supertype = (int32_t)supertype;
    }
    if (form == 0x60) {
        uint32_t params, results;
        type->kind = WAST_TYPE_FUNC;
        if (!wasm_reader_read_u32(sec, &params) || params > EXEC_MAX_LOCALS)
            return exec_fail(err, EXEC_ERROR_FORMAT, "too many params");
        type->param_count = (int)params;
        for (uint32_t i = 0; i < params; i++)
            if (!wasm_decode_valtype(sec, &type->params[i]))
                return exec_fail(err, EXEC_ERROR_UNSUPPORTED,
                                 "unsupported param type");
        if (!wasm_reader_read_u32(sec, &results) || results > WAST_MAX_RESULTS)
            return exec_fail(err, EXEC_ERROR_FORMAT,
                             "unsupported result count");
        type->result_count = (int)results;
        for (uint32_t i = 0; i < results; i++)
            if (!wasm_decode_valtype(sec, &type->results[i]))
                return exec_fail(err, EXEC_ERROR_UNSUPPORTED,
                                 "unsupported result type");
        return EXEC_OK;
    }
    if (form == 0x5f) {
        uint32_t fields;
        type->kind = WAST_TYPE_STRUCT;
        if (!wasm_reader_read_u32(sec, &fields) || fields > WAST_MAX_TYPE_FIELDS)
            return exec_fail(err, EXEC_ERROR_FORMAT,
                             "invalid struct field count");
        type->field_count = (int)fields;
        for (uint32_t i = 0; i < fields; i++) {
            uint8_t mutable_;
            if (!parse_storage_type(sec, &type->fields[i],
                                    &type->field_packed[i]) ||
                !wasm_reader_read_u8(sec, &mutable_) || mutable_ > 1)
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid struct field type");
            type->field_mutable[i] = mutable_;
        }
        return EXEC_OK;
    }
    if (form == 0x5e) {
        uint8_t mutable_;
        type->kind = WAST_TYPE_ARRAY;
        type->field_count = 1;
        if (!parse_storage_type(sec, &type->fields[0],
                                &type->field_packed[0]) ||
            !wasm_reader_read_u8(sec, &mutable_) || mutable_ > 1)
            return exec_fail(err, EXEC_ERROR_FORMAT,
                             "invalid array field type");
        type->field_mutable[0] = mutable_;
        return EXEC_OK;
    }
    return exec_fail(err, EXEC_ERROR_FORMAT, "invalid composite type");
}

static int type_reference_in_scope(const exec_func_type *type,
                                   wasm_valtype value_type,
                                   uint32_t total_types) {
    if (!WASM_VALTYPE_IS_TYPE_REF(value_type)) return 1;
    uint32_t target = WASM_VALTYPE_TYPE_REF_INDEX(value_type);
    uint32_t group_end = type->rec_group_start + type->rec_group_size;
    return target < total_types && target < group_end;
}


static exec_status parse_types(waste_exec_engine *eng, wasm_reader *sec, exec_error *err) {
    uint32_t entries;
    if (!wasm_reader_read_u32(sec, &entries))
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid type count");
    if (entries > EXEC_MAX_TYPES)
        return exec_fail(err, EXEC_ERROR_FORMAT, "too many types");
    eng->types = (exec_func_type *)calloc(EXEC_MAX_TYPES, sizeof(*eng->types));
    if (entries && !eng->types)
        return exec_fail(err, EXEC_ERROR_FORMAT, "type alloc failed");
    uint32_t index = 0;
    for (uint32_t entry = 0; entry < entries; entry++) {
        uint8_t form;
        if (!wasm_reader_read_u8(sec, &form))
            return exec_fail(err, EXEC_ERROR_FORMAT, "truncated type entry");
        uint32_t group_size = 1;
        if (form == 0x4e) {
            if (!wasm_reader_read_u32(sec, &group_size) || !group_size)
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid recursive type group");
        }
        if (group_size > EXEC_MAX_TYPES - index)
            return exec_fail(err, EXEC_ERROR_FORMAT, "too many types");
        uint32_t group_start = index;
        for (uint32_t member = 0; member < group_size; member++, index++) {
            if (form == 0x4e && !wasm_reader_read_u8(sec, &form))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "truncated recursive type group");
            exec_status status = parse_composite_type(
                eng, sec, form, index, group_start, group_size, err);
            if (status != EXEC_OK) return status;
            form = 0x4e;
        }
    }
    eng->type_count = index;
    for (uint32_t i = 0; i < eng->type_count; i++) {
        exec_func_type *type = &eng->types[i];
        for (int p = 0; p < type->param_count; p++)
            if (!type_reference_in_scope(type, type->params[p], eng->type_count))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "type reference outside recursive group");
        for (int result = 0; result < type->result_count; result++)
            if (!type_reference_in_scope(type, type->results[result],
                                         eng->type_count))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "type reference outside recursive group");
        for (int field = 0; field < type->field_count; field++)
            if (!type_reference_in_scope(type, type->fields[field],
                                         eng->type_count))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "type reference outside recursive group");
        if (!validate_declared_subtype(eng, i)) {
            if (err) {
                err->status = EXEC_ERROR_FORMAT;
                snprintf(err->message, sizeof(err->message),
                         "invalid declared subtype %u (super %d, final %u)",
                         i, type->supertype, (unsigned)type->is_final);
            }
            return EXEC_ERROR_FORMAT;
        }
    }
    return EXEC_OK;
}

static exec_status instantiate_imports(waste_exec_engine *eng,
                                       const wasm_module *module,
                                       const exec_imports *imports,
                                       exec_error *err) {
    if (!module || module->import_count > EXEC_MAX_FUNCS)
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid import count");
    for (uint32_t i = 0; i < module->import_count; i++) {
        const wasm_import *declaration = &module->imports[i];
        const char *module_name = declaration->module;
        const char *name = declaration->name;
        if (declaration->kind == WASM_IMPORT_FUNCTION) {
            uint32_t type_index = declaration->descriptor.function.type_index;
            if (type_index >= eng->type_count ||
                eng->types[type_index].kind != WAST_TYPE_FUNC ||
                eng->import_func_count >= EXEC_MAX_FUNCS)
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid function import type");
            const exec_host_import *binding =
                find_host_import(imports, module_name, name);
            if (!binding || !binding->function) return exec_fail(err, EXEC_ERROR_NOT_FOUND, "unresolved function import");
            if(binding->has_wasm_type&&!func_type_is_subtype(
                    binding->type_owner,binding->type_index,eng,type_index))
                return exec_fail(err,EXEC_ERROR_FORMAT,"function import type mismatch");
            uint32_t index=eng->import_func_count++;
            eng->import_func_types[index]=type_index; eng->import_funcs[index]=binding->function;
            eng->import_host_data[index]=binding->host_data;
            eng->import_controls[index]=binding->control;
        } else if (declaration->kind == WASM_IMPORT_TABLE) {
            wasm_valtype type = declaration->descriptor.table.element_type;
            const wasm_import_limits *limits =
                &declaration->descriptor.table.limits;
            uint32_t flags = limits->flags;
            uint64_t initial = limits->minimum;
            uint64_t maximum = limits->maximum;
            if (!value_type_is_defined(eng, type) ||
                !is_reference_type(type) || (flags & 0xfau) ||
                (!(flags&4u) && (initial > UINT32_MAX || ((flags&1u) && maximum > UINT32_MAX))) ||
                ((flags&1u) && maximum<initial) ||
                eng->table_count>=EXEC_MAX_TABLES) return exec_fail(err,EXEC_ERROR_FORMAT,"invalid table import type");
            exec_table *table=find_table_import(imports,module_name,name);
            if (!table) return exec_fail(err,EXEC_ERROR_NOT_FOUND,"unresolved table import");
            if (table->is_64 != ((flags & 4u) != 0) ||
                !same_value_type(eng,type,table->type_owner,table->element_type,0) ||
                table->size<initial || ((flags&1u) && (!table->has_max || table->max_size>maximum)))
                return exec_fail(err,EXEC_ERROR_FORMAT,"table import type mismatch");
            eng->tables[eng->table_count++]=table; eng->import_table_count++;
        } else if (declaration->kind == WASM_IMPORT_MEMORY) {
            const wasm_import_limits *limits =
                &declaration->descriptor.memory.limits;
            uint32_t flags = limits->flags;
            uint64_t initial = limits->minimum;
            uint64_t maximum = limits->maximum;
            if ((flags & 0xfau) ||
                initial > ((flags&4u) ? UINT64_C(0x1000000000000) : UINT64_C(65536)) ||
                ((flags&1u) && (maximum < initial ||
                    maximum > ((flags&4u) ? UINT64_C(0x1000000000000) : UINT64_C(65536)))) ||
                eng->memory_count >= WAST_MAX_MEMORIES)
                return exec_fail(err,EXEC_ERROR_FORMAT,"invalid memory import type");
            exec_memory *memory=find_memory_import(imports,module_name,name);
            if (!memory) return exec_fail(err,EXEC_ERROR_NOT_FOUND,"unresolved memory import");
            if (memory->is_64 != ((flags & 4u) != 0) || memory->pages<initial ||
                ((flags&1u) && (!memory->has_max || memory->max_pages>maximum)) ||
                (memory->pages && !memory->data)) return exec_fail(err,EXEC_ERROR_FORMAT,"memory import type mismatch");
            eng->memories[eng->memory_count++] = memory;
            eng->import_memory_count++;
            if (!eng->memory) eng->memory=memory;
        } else if (declaration->kind == WASM_IMPORT_GLOBAL) {
            uint8_t mutability = declaration->descriptor.global.mutable_;
            wasm_valtype value_type =
                declaration->descriptor.global.value_type;
            if (!value_type_is_defined(eng, value_type) ||
                mutability>1 || eng->global_count>=EXEC_MAX_GLOBALS)
                return exec_fail(err,EXEC_ERROR_FORMAT,"invalid global import type");
            exec_global *global=find_global_import(imports,module_name,name);
            if (!global) return exec_fail(err,EXEC_ERROR_NOT_FOUND,"unresolved global import");
            if (!global_type_is_compat(global->type_owner,global->value.type,eng,value_type,mutability) ||
                global->mutable_!=mutability)
                return exec_fail(err,EXEC_ERROR_FORMAT,"global import type mismatch");
            eng->globals[eng->global_count++]=global; eng->import_global_count++;
        } else if (declaration->kind == WASM_IMPORT_TAG) {
            uint32_t attribute = declaration->descriptor.tag.attribute;
            uint32_t type_index = declaration->descriptor.tag.type_index;
            if (attribute != 0 || type_index >= eng->type_count ||
                eng->types[type_index].kind != WAST_TYPE_FUNC ||
                eng->types[type_index].result_count != 0 ||
                eng->tag_count >= WAST_MAX_TAGS)
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid tag import");
            exec_tag *tag = find_tag_import(imports, module_name, name);
            if (!tag)
                return exec_fail(err, EXEC_ERROR_NOT_FOUND,
                                 "unresolved tag import");
            if (!same_func_type(eng, type_index, tag->type_owner,
                                tag->type_index))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "tag import type mismatch");
            eng->tag_types[eng->tag_count] = type_index;
            eng->tags[eng->tag_count++] = tag;
            eng->import_tag_count++;
        } else {
            return exec_fail(err,EXEC_ERROR_FORMAT,"invalid import kind");
        }
    }
    return EXEC_OK;
}

/* ---- Function section ---- */

static exec_status parse_funcs(waste_exec_engine *eng, wasm_reader *sec, exec_error *err) {
    uint32_t count;
    if (!wasm_reader_read_u32(sec, &count))
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid function count");
    if (count > EXEC_MAX_FUNCS) {
        if (err) {
            err->status = EXEC_ERROR_FORMAT;
            snprintf(err->message, sizeof(err->message),
                     "too many functions: %u exceeds %u", count,
                     (unsigned)EXEC_MAX_FUNCS);
        }
        return EXEC_ERROR_FORMAT;
    }
    eng->funcs = (exec_func *)calloc(count, sizeof(*eng->funcs));
    if (count && !eng->funcs)
        return exec_fail(err, EXEC_ERROR_FORMAT, "func alloc failed");
    eng->func_count = count;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t ti;
        if (!wasm_reader_read_u32(sec, &ti) || ti >= eng->type_count ||
            eng->types[ti].kind != WAST_TYPE_FUNC)
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid type index");
        eng->funcs[i].type_index = ti;
    }
    return EXEC_OK;
}

/* ---- Export section ---- */

static exec_status parse_exports(waste_exec_engine *eng, wasm_reader *sec, exec_error *err) {
    uint32_t count;
    if (!wasm_reader_read_u32(sec, &count))
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid export count");
    if (count > EXEC_MAX_EXPORTS)
        return exec_fail(err, EXEC_ERROR_FORMAT, "too many exports");
    eng->exports = (exec_export *)calloc(count, sizeof(*eng->exports));
    if (count && !eng->exports)
        return exec_fail(err, EXEC_ERROR_FORMAT, "export alloc failed");
    eng->export_count = count;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t name_len;
        const uint8_t *name;
        uint8_t kind;
        uint32_t idx;
        if (!wasm_reader_read_u32(sec, &name_len) || name_len >= EXEC_MAX_NAME)
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid export name length");
        if (!wasm_reader_read_bytes(sec, name_len, &name))
            return exec_fail(err, EXEC_ERROR_FORMAT, "truncated export name");
        if (!valid_utf8(name, name_len))
            return exec_fail(err, EXEC_ERROR_FORMAT, "malformed UTF-8 encoding");
        if (!wasm_reader_read_u8(sec, &kind) || kind > 4 || !wasm_reader_read_u32(sec, &idx))
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid export");
        if ((kind==0 && idx>=eng->import_func_count+eng->func_count) ||
            (kind==1 && idx>=eng->table_count) ||
            (kind==2 && idx>=eng->memory_count) ||
            (kind==3 && idx>=eng->global_count) ||
            (kind==4 && idx>=eng->tag_count)) return exec_fail(err,EXEC_ERROR_FORMAT,"invalid export index");
        memcpy(eng->exports[i].name, name, name_len);
        eng->exports[i].name[name_len] = '\0';
        eng->exports[i].index = idx; eng->exports[i].kind=kind;
        if (kind == 0) eng->declared_funcs[idx] = 1;
        for (uint32_t j = 0; j < i; j++)
            if (strcmp(eng->exports[j].name, eng->exports[i].name) == 0)
                return exec_fail(err, EXEC_ERROR_FORMAT, "duplicate export name");
    }
    return EXEC_OK;
}

static exec_status parse_memory(waste_exec_engine *eng, wasm_reader *sec, exec_error *err) {
    uint32_t count;
    if (!wasm_reader_read_u32(sec, &count) || count > WAST_MAX_MEMORIES - eng->memory_count)
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid memory count");
    for (uint32_t i = 0; i < count; i++) {
        uint64_t initial, maximum = 0;
        uint8_t flags;
        if (!wasm_reader_read_u8(sec, &flags) || (flags & 0xfau) || !wasm_reader_read_u64(sec, &initial) ||
            ((flags & 1u) && !wasm_reader_read_u64(sec, &maximum)) ||
            initial > ((flags&4u) ? UINT64_C(0x1000000000000) : UINT64_C(65536)) ||
            ((flags & 1u) &&
             (maximum > ((flags&4u) ? UINT64_C(0x1000000000000) : UINT64_C(65536)) ||
              maximum < initial)))
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid memory limits");
        uint32_t index = eng->memory_count;
        exec_memory *memory = &eng->owned_memories[index];
        exec_status status = wasm_instance_allocate_memory(
            memory, initial, maximum, flags, err);
        if (status != EXEC_OK) return status;
        eng->memories[index]=memory;
        eng->owns_memories[index]=1;
        eng->memory_count++;
        if (!eng->memory) eng->memory=memory;
    }
    return EXEC_OK;
}

typedef struct {
    wasm_value value;
    const waste_exec_engine *type_owner;
} exec_const_value;

static exec_status instantiate_constexpr(waste_exec_engine *eng,
                                  wasm_reader *sec,
                                  uint32_t global_limit,
                                  wasm_valtype declared_type,
                                  exec_const_value *result,
                                  exec_error *err);

static exec_status parse_tables(waste_exec_engine *eng, wasm_reader *sec, exec_error *err) {
    uint32_t count;
    if (!wasm_reader_read_u32(sec,&count) || count>EXEC_MAX_TABLES-eng->table_count) return exec_fail(err,EXEC_ERROR_FORMAT,"invalid table count");
    for(uint32_t i=0;i<count;i++) {
        uint8_t has_initializer = 0;
        wasm_valtype type; uint8_t flags; uint64_t initial,maximum=0;
        /* The typed-function-references table form is encoded as
         * 0x40 0x00 tabletype constexpr.  The reserved zero distinguishes it
         * from the legacy tabletype whose first byte is a reftype. */
        if (sec->cursor < sec->end && *sec->cursor == 0x40) {
            uint8_t marker, reserved;
            if (!wasm_reader_read_u8(sec, &marker) || !wasm_reader_read_u8(sec, &reserved) || reserved != 0)
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid table initializer marker");
            has_initializer = 1;
        }
        if (!wasm_decode_valtype(sec,&type) || !value_type_is_defined(eng, type) ||
            !is_reference_type(type) || !wasm_reader_read_u8(sec,&flags) || (flags&0xfau) ||
            !wasm_reader_read_u64(sec,&initial) || ((flags&1u) && !wasm_reader_read_u64(sec,&maximum)) ||
            (!(flags&4u) && (initial>UINT32_MAX || ((flags&1u) && maximum>UINT32_MAX))) ||
            ((flags&1u) && maximum<initial))
            return exec_fail(err,EXEC_ERROR_FORMAT,"invalid table type");
        if (!has_initializer && !is_nullable_reference_type(type))
            return exec_fail(err, EXEC_ERROR_FORMAT,
                             "non-nullable table requires initializer");
        uint32_t index=eng->table_count; exec_table *table=&eng->owned_tables[index];
        exec_status allocation_status = wasm_instance_allocate_table(
            table, eng, type, initial, maximum, flags, err);
        if (allocation_status != EXEC_OK) return allocation_status;
        eng->tables[eng->table_count++]=table;
        if (has_initializer) {
            exec_const_value value;
            exec_status status = instantiate_constexpr(
                eng, sec, eng->global_count, type, &value, err);
            if (status != EXEC_OK) return status;
            for (uint64_t j = 0; j < initial; j++) {
                if (value.value.ref != UINT32_MAX) {
                    table->elements[j].owner =
                        (waste_exec_engine *)value.type_owner;
                    table->elements[j].func_idx = value.value.ref;
                    table->elements[j].type = value.value.type;
                    table->elements[j].dynamic_type =
                        reference_dynamic_type(&value.value);
                }
            }
        }
    }
    return EXEC_OK;
}


static exec_status instantiate_constexpr(waste_exec_engine *eng,
                                  wasm_reader *sec,
                                  uint32_t global_limit,
                                  wasm_valtype declared_type,
                                  exec_const_value *result,
                                  exec_error *err) {
    exec_const_value stack[32];
    int top = 0;
    for (;;) {
        uint8_t opcode;
        if (!wasm_reader_read_u8(sec, &opcode))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "unterminated constant expression");
        if (opcode == 0x0b) break;
        exec_const_value value;
        memset(&value, 0, sizeof(value));
        value.type_owner = eng;
        if (opcode == 0x41) {
            int32_t immediate;
            if (!wasm_reader_read_i32(sec, &immediate))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid i32 global initializer");
            value.value.type = WASM_VALTYPE_I32;
            value.value.i32 = immediate;
        } else if (opcode == 0x42) {
            int64_t immediate;
            if (!wasm_reader_read_i64(sec, &immediate))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid i64 global initializer");
            value.value.type = WASM_VALTYPE_I64;
            value.value.i64 = immediate;
        } else if (opcode == 0x43 || opcode == 0x44) {
            const uint8_t *bits;
            size_t width = opcode == 0x43 ? 4u : 8u;
            if (!wasm_reader_read_bytes(sec, width, &bits))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid float global initializer");
            value.value.type = opcode == 0x43 ? WASM_VALTYPE_F32 :
                                               WASM_VALTYPE_F64;
            if (opcode == 0x43) memcpy(&value.value.f32, bits, 4);
            else memcpy(&value.value.f64, bits, 8);
        } else if (opcode == 0x23) {
            uint32_t source;
            if (!wasm_reader_read_u32(sec, &source) || source >= global_limit ||
                !eng->globals[source] || eng->globals[source]->mutable_)
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid global.get initializer");
            value.value = eng->globals[source]->value;
            value.type_owner = eng->globals[source]->type_owner;
        } else if (opcode == 0xd0) {
            int32_t heap_type;
            if (!wasm_reader_read_i32(sec, &heap_type) ||
                !nullable_reference_for_heap(eng, heap_type,
                                             &value.value.type))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid ref.null initializer");
            value.value.ref = UINT32_MAX;
        } else if (opcode == 0xd2) {
            uint32_t function, type_index;
            if (!wasm_reader_read_u32(sec, &function) ||
                function >= eng->import_func_count + eng->func_count)
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid ref.func initializer");
            type_index = function < eng->import_func_count ?
                eng->import_func_types[function] :
                eng->funcs[function - eng->import_func_count].type_index;
            value.value.type = (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE +
                                              type_index);
            value.value.ref = function;
            eng->declared_funcs[function] = 1;
        } else if (opcode == 0xfd) {
            uint32_t simd_op;
            const uint8_t *bytes;
            if (!wasm_reader_read_u32(sec, &simd_op) || simd_op != 0x0c ||
                !wasm_reader_read_bytes(sec, 16, &bytes))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid v128 global initializer");
            value.value.type = WASM_VALTYPE_V128;
            memcpy(value.value.v128.bytes, bytes, 16);
        } else if (opcode == 0xfb) {
            uint32_t subopcode, type_index = 0;
            if (!wasm_reader_read_u32(sec, &subopcode))
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid GC initializer");
            if (subopcode == 0x1a || subopcode == 0x1b) {
                if (top < 1 || !is_reference_type(stack[top - 1].value.type))
                    return exec_fail(err, EXEC_ERROR_FORMAT,
                                     "GC initializer type mismatch");
                value = stack[--top];
                wasm_valtype dynamic = reference_dynamic_type(&value.value);
                value.value.type = subopcode == 0x1a ?
                    (value.value.ref == UINT32_MAX ? WASM_VALTYPE_ANYREF :
                     (WASM_VALTYPE_IS_TYPE_REF(dynamic) ||
                      dynamic == WASM_VALTYPE_I31REF_NONNULL ||
                      dynamic == WASM_VALTYPE_STRUCTREF_NONNULL ||
                      dynamic == WASM_VALTYPE_ARRAYREF_NONNULL ?
                        dynamic : WASM_VALTYPE_ANYREF_NONNULL)) :
                    (value.value.ref == UINT32_MAX ? WASM_VALTYPE_EXTERNREF :
                                                    WASM_VALTYPE_EXTERNREF_NONNULL);
                set_reference_dynamic_type(&value.value, dynamic);
            } else if (subopcode == 0x1c) {
                if (top < 1 || stack[top - 1].value.type != WASM_VALTYPE_I32)
                    return exec_fail(err, EXEC_ERROR_FORMAT,
                                     "ref.i31 initializer type mismatch");
                value.value.type = WASM_VALTYPE_I31REF_NONNULL;
                value.value.ref = (uint32_t)stack[--top].value.i32 &
                                  UINT32_C(0x7fffffff);
                set_reference_dynamic_type(&value.value,
                                           WASM_VALTYPE_I31REF_NONNULL);
            } else if (subopcode == 0x00 || subopcode == 0x01) {
                if (!wasm_reader_read_u32(sec, &type_index) || type_index >= eng->type_count ||
                    eng->types[type_index].kind != WAST_TYPE_STRUCT)
                    return exec_fail(err, EXEC_ERROR_FORMAT,
                                     "invalid struct initializer type");
                exec_func_type *type = &eng->types[type_index];
                int operands = subopcode == 0x00 ? type->field_count : 0;
                if (top < operands ||
                    !gc_allocate(eng, WAST_TYPE_STRUCT, type_index,
                                 (uint32_t)type->field_count, &value.value, err))
                    return err && err->status ? err->status : EXEC_ERROR_FORMAT;
                exec_gc_object *object =
                    &eng->gc_objects[value.value.ref & ~UINT32_C(0x80000000)];
                for (int i = type->field_count; i-- > 0;) {
                    wasm_valtype field_type = type->fields[i];
                    if (subopcode == 0x00) {
                        exec_const_value operand = stack[--top];
                        if ((type->field_packed[i] &&
                             operand.value.type != WASM_VALTYPE_I32) ||
                            (!type->field_packed[i] &&
                             !global_type_is_compat(operand.type_owner,
                                 operand.value.type, eng, field_type, 0)))
                            return exec_fail(err, EXEC_ERROR_FORMAT,
                                             "struct initializer type mismatch");
                        object->values[i] = operand.value;
                    } else {
                        object->values[i] = default_value(field_type);
                    }
                }
                set_reference_dynamic_type(&value.value, value.value.type);
            } else if (subopcode == 0x06 || subopcode == 0x07 ||
                       subopcode == 0x08) {
                uint32_t length;
                if (!wasm_reader_read_u32(sec, &type_index) || type_index >= eng->type_count ||
                    eng->types[type_index].kind != WAST_TYPE_ARRAY)
                    return exec_fail(err, EXEC_ERROR_FORMAT,
                                     "invalid array initializer type");
                exec_func_type *type = &eng->types[type_index];
                if (subopcode == 0x08) {
                    if (!wasm_reader_read_u32(sec, &length) || top < (int)length)
                        return exec_fail(err, EXEC_ERROR_FORMAT,
                                         "invalid array.new_fixed initializer");
                } else {
                    if (top < (subopcode == 0x06 ? 2 : 1) ||
                        stack[top - 1].value.type != WASM_VALTYPE_I32)
                        return exec_fail(err, EXEC_ERROR_FORMAT,
                                         "array initializer length mismatch");
                    length = (uint32_t)stack[--top].value.i32;
                }
                if (!gc_allocate(eng, WAST_TYPE_ARRAY, type_index, length,
                                 &value.value, err))
                    return err && err->status ? err->status : EXEC_ERROR_FORMAT;
                exec_gc_object *object =
                    &eng->gc_objects[value.value.ref & ~UINT32_C(0x80000000)];
                if (subopcode == 0x07) {
                    wasm_value initial = default_value(type->fields[0]);
                    for (uint32_t i = 0; i < length; i++) object->values[i] = initial;
                } else if (subopcode == 0x06) {
                    exec_const_value initial = stack[--top];
                    if ((type->field_packed[0] &&
                         initial.value.type != WASM_VALTYPE_I32) ||
                        (!type->field_packed[0] &&
                         !global_type_is_compat(initial.type_owner,
                             initial.value.type, eng, type->fields[0], 0)))
                        return exec_fail(err, EXEC_ERROR_FORMAT,
                                         "array initializer type mismatch");
                    for (uint32_t i = 0; i < length; i++)
                        object->values[i] = initial.value;
                } else {
                    for (uint32_t i = length; i-- > 0;)
                        object->values[i] = stack[--top].value;
                }
                set_reference_dynamic_type(&value.value, value.value.type);
            } else {
                return exec_fail(err, EXEC_ERROR_UNSUPPORTED,
                                 "unsupported GC initializer");
            }
        } else if (opcode == 0x6a || opcode == 0x6b || opcode == 0x6c ||
                   opcode == 0x7c || opcode == 0x7d || opcode == 0x7e) {
            wasm_valtype operand_type = opcode < 0x7c ? WASM_VALTYPE_I32 :
                                                       WASM_VALTYPE_I64;
            if (top < 2 || stack[top - 1].value.type != operand_type ||
                stack[top - 2].value.type != operand_type)
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "global initializer type mismatch");
            exec_const_value right = stack[--top];
            exec_const_value *left = &stack[top - 1];
            if (operand_type == WASM_VALTYPE_I32) {
                uint32_t a = (uint32_t)left->value.i32;
                uint32_t b = (uint32_t)right.value.i32;
                left->value.i32 = (int32_t)(opcode == 0x6a ? a + b :
                                            opcode == 0x6b ? a - b : a * b);
            } else {
                uint64_t a = (uint64_t)left->value.i64;
                uint64_t b = (uint64_t)right.value.i64;
                left->value.i64 = (int64_t)(opcode == 0x7c ? a + b :
                                            opcode == 0x7d ? a - b : a * b);
            }
            continue;
        } else if (opcode == 0xac || opcode == 0xad) {
            if (top < 1 || stack[top - 1].value.type != WASM_VALTYPE_I32)
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "global initializer type mismatch");
            int32_t input = stack[top - 1].value.i32;
            stack[top - 1].value.type = WASM_VALTYPE_I64;
            stack[top - 1].value.i64 = opcode == 0xac ?
                (int64_t)input : (int64_t)(uint32_t)input;
            continue;
        } else {
            return exec_fail(err, EXEC_ERROR_UNSUPPORTED,
                             "unsupported global initializer");
        }
        if (top >= (int)(sizeof(stack) / sizeof(stack[0])))
            return exec_fail(err, EXEC_ERROR_FORMAT,
                             "global initializer stack overflow");
        stack[top++] = value;
    }
    if (top != 1 ||
        !global_type_is_compat(stack[0].type_owner, stack[0].value.type,
                               eng, declared_type, 0)) {
        if (err) {
            err->status = EXEC_ERROR_FORMAT;
            snprintf(err->message, sizeof(err->message),
                     "initializer type mismatch: expected %d, got %d (stack %d)",
                     (int)declared_type,
                     top == 1 ? (int)stack[0].value.type : -1, top);
        }
        return EXEC_ERROR_FORMAT;
    }
    *result = stack[0];
    return EXEC_OK;
}

static exec_status parse_globals(waste_exec_engine *eng, wasm_reader *sec, exec_error *err) {
    uint32_t count;
    if (!wasm_reader_read_u32(sec, &count) || count > EXEC_MAX_GLOBALS) return exec_fail(err, EXEC_ERROR_FORMAT, "invalid global count");
    if (count > EXEC_MAX_GLOBALS-eng->global_count) return exec_fail(err,EXEC_ERROR_FORMAT,"too many globals");
    for (uint32_t i = 0; i < count; i++) {
        uint32_t index=eng->global_count+i; exec_global *global=&eng->owned_globals[index];
        wasm_valtype type; uint8_t mutability;
        if (!wasm_decode_valtype(sec, &type) || !value_type_is_defined(eng, type) ||
            !wasm_reader_read_u8(sec, &mutability) || mutability > 1)
            return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported global initializer");
        exec_const_value initial;
        exec_status status = instantiate_constexpr(eng, sec, index, type,
                                            &initial, err);
        if (status != EXEC_OK) return status;
        /* A global's externally visible type is its declared type, not the
         * possibly narrower type of its initializer.  In particular, a
         * (ref.func ...) initializer must not turn a declared (ref func)
         * global into a specific indexed reference type for later imports.
         * The reference payload is unchanged; only its static type is
         * canonicalized at the global boundary. */
        wasm_instance_initialize_global(global, eng, type, mutability,
                                        &initial.value);
        eng->globals[index]=global;
    }
    eng->global_count += count;
    return EXEC_OK;
}

static exec_status parse_tags(waste_exec_engine *eng, wasm_reader *sec,
                              exec_error *err) {
    uint32_t count;
    if (!wasm_reader_read_u32(sec, &count) || count > WAST_MAX_TAGS - eng->tag_count)
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid tag count");
    for (uint32_t i = 0; i < count; i++) {
        uint32_t attribute, type_index;
        if (!wasm_reader_read_u32(sec, &attribute) || attribute != 0 ||
            !wasm_reader_read_u32(sec, &type_index) || type_index >= eng->type_count ||
            eng->types[type_index].kind != WAST_TYPE_FUNC ||
            eng->types[type_index].result_count != 0)
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid tag type");
        uint32_t index = eng->tag_count;
        exec_tag *tag = &eng->owned_tags[index];
        wasm_instance_initialize_tag(tag, eng, type_index);
        eng->tag_types[index] = type_index;
        eng->tags[index] = tag;
        eng->tag_count++;
    }
    return EXEC_OK;
}

static exec_status parse_start(waste_exec_engine *eng, wasm_reader *sec, exec_error *err) {
    uint32_t func_index;
    if (!wasm_reader_read_u32(sec, &func_index) || func_index >= eng->import_func_count + eng->func_count)
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid start function index");
    uint32_t type_index = func_index < eng->import_func_count ?
        eng->import_func_types[func_index] :
        eng->funcs[func_index - eng->import_func_count].type_index;
    if (type_index >= eng->type_count ||
        eng->types[type_index].param_count != 0 ||
        eng->types[type_index].result_count != 0)
        return exec_fail(err, EXEC_ERROR_FORMAT, "start function must have type () -> ()");
    eng->start_func = func_index;
    eng->has_start = 1;
    return EXEC_OK;
}

static exec_status parse_data(waste_exec_engine *eng, wasm_reader *sec, exec_error *err) {
    uint32_t count;
    if (!wasm_reader_read_u32(sec, &count)) return exec_fail(err, EXEC_ERROR_FORMAT, "invalid data count");
    if (eng->has_data_count && count != eng->declared_data_count)
        return exec_fail(err, EXEC_ERROR_FORMAT, "data count mismatch");
    if (count > WAST_MAX_DATA_SEGS)
        return exec_fail(err, EXEC_ERROR_FORMAT, "too many data segments");
    eng->data_count = count;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t mode, memory_index = 0, length;
        uint64_t offset;
        const uint8_t *data;
        if (!wasm_reader_read_u32(sec, &mode)) return exec_fail(err, EXEC_ERROR_FORMAT, "invalid data segment");
        if (mode == 1) {
            /* passive segment — save bytes for memory.init */
            if (!wasm_reader_read_u32(sec, &length) || !wasm_reader_read_bytes(sec, length, &data))
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid passive data");
            exec_status allocation_status = wasm_instance_copy_segment(
                &eng->data_segs[i], data, length, err);
            if (allocation_status != EXEC_OK) return allocation_status;
            eng->data_seg_lengths[i] = length;
            continue;
        }
        if (mode == 2 && !wasm_reader_read_u32(sec, &memory_index)) return exec_fail(err, EXEC_ERROR_FORMAT, "invalid data memory");
        if ((mode != 0 && mode != 2) || memory_index >= eng->memory_count)
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid active data segment");
        exec_memory *memory = eng->memories[memory_index];
        exec_const_value initial;
        exec_status status = instantiate_constexpr(eng, sec, eng->global_count,
                                            memory->is_64 ? WASM_VALTYPE_I64 : WASM_VALTYPE_I32,
                                            &initial, err);
        if (status != EXEC_OK)
            return status;
        offset = memory->is_64 ? (uint64_t)initial.value.i64 :
                                (uint64_t)(uint32_t)initial.value.i32;
        if (!wasm_reader_read_u32(sec, &length) || !wasm_reader_read_bytes(sec, length, &data))
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid active data segment");
        uint64_t memory_size = (uint64_t)memory->pages * EXEC_PAGE_SIZE;
        if (offset > memory_size || length > memory_size - offset) {
            if (!eng->instantiation_trapped) {
                eng->instantiation_trapped = 1;
                snprintf(eng->instantiation_error,
                         sizeof(eng->instantiation_error),
                         "out of bounds memory access");
            }
        } else if (!eng->instantiation_trapped) {
            memcpy(memory->data + (size_t)offset, data, length);
        }
        /* active segments are considered dropped after instantiation */
        eng->data_dropped[i] = 1;
    }
    return EXEC_OK;
}

static exec_status parse_elements(waste_exec_engine *eng, wasm_reader *sec,
                                  exec_error *err) {
    uint32_t count;
    if (!wasm_reader_read_u32(sec, &count) || count > WAST_MAX_ELEM_SEGS)
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element count");
    eng->elem_count = count;
    for (uint32_t segment = 0; segment < count; segment++) {
        uint32_t mode, table_index = 0, item_count;
        uint64_t offset = 0;
        wasm_valtype ref_type = WASM_VALTYPE_FUNCREF;
        int active, uses_expressions;
        if (!wasm_reader_read_u32(sec, &mode) || mode > 7)
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element segment mode");
        /* Modes 0-3: funcidx vectors; modes 4-7: expression vectors */
        uses_expressions = mode >= 4;
        active = (mode == 0 || mode == 2 || mode == 4 || mode == 6);
        /* Read explicit table index for modes 2 and 6 */
        if ((mode == 2 || mode == 6) && !wasm_reader_read_u32(sec, &table_index))
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element table");
        /* Read offset expression for active segments */
        if (active) {
            exec_const_value initial;
            if (table_index >= eng->table_count)
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid active element segment");
            exec_status status = instantiate_constexpr(eng, sec, eng->global_count,
                                                eng->tables[table_index]->is_64 ?
                                                    WASM_VALTYPE_I64 : WASM_VALTYPE_I32,
                                                &initial,
                                                err);
            if (status != EXEC_OK) return status;
            offset = eng->tables[table_index]->is_64 ?
                (uint64_t)initial.value.i64 :
                (uint64_t)(uint32_t)initial.value.i32;
        }
        /* Read type/kind: modes 1,2,3 have elemkind; modes 5,6,7 have reftype;
           modes 0,4 imply funcref */
        if (mode == 1 || mode == 2 || mode == 3) {
            uint8_t elemkind;
            if (!wasm_reader_read_u8(sec, &elemkind) || elemkind != 0x00)
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element kind");
            ref_type = WASM_VALTYPE_FUNCREF;
        } else if (mode == 5 || mode == 6 || mode == 7) {
            if (!wasm_decode_valtype(sec, &ref_type) ||
                !value_type_is_defined(eng, ref_type) ||
                !is_reference_type(ref_type))
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element type");
        }
        /* else mode 0 or 4: ref_type stays FUNCREF */
        if (!wasm_reader_read_u32(sec, &item_count))
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element length");
        eng->elem_types[segment] = ref_type;
        eng->elem_lengths[segment] = item_count;
        eng->elem_dropped[segment] =
            (uint8_t)(active || mode == 3 || mode == 7);
        exec_status allocation_status = wasm_instance_allocate_elements(
            &eng->elem_values[segment], item_count, err);
        if (allocation_status != EXEC_OK) return allocation_status;
        if (active && table_index >= eng->table_count)
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid active element segment");
        if (uses_expressions) {
            /* Modes 4-7: each item is an init expression */
            for (uint32_t item = 0; item < item_count; item++) {
                exec_table_element slot = {NULL, 0, ref_type, ref_type};
                exec_const_value value;
                exec_status status = instantiate_constexpr(eng, sec,
                                                    eng->global_count,
                                                    ref_type, &value, err);
                if (status != EXEC_OK) return status;
                if (value.value.ref != UINT32_MAX) {
                    slot.owner = (waste_exec_engine *)value.type_owner;
                    slot.func_idx = value.value.ref;
                    slot.type = value.value.type;
                    slot.dynamic_type = reference_dynamic_type(&value.value);
                }
                eng->elem_values[segment][item] = slot;
            }
        } else {
            /* Modes 0-3: each item is a bare funcidx */
            for (uint32_t item = 0; item < item_count; item++) {
                uint32_t func_idx;
                if (!wasm_reader_read_u32(sec, &func_idx) ||
                    func_idx >= eng->import_func_count + eng->func_count)
                    return exec_fail(err, EXEC_ERROR_FORMAT, "invalid element funcidx");
                exec_table_element slot = {eng, func_idx,
                    (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE +
                        (func_idx < eng->import_func_count ?
                         eng->import_func_types[func_idx] :
                         eng->funcs[func_idx - eng->import_func_count].type_index)),
                    (wasm_valtype)(WASM_VALTYPE_TYPE_REF_BASE +
                        (func_idx < eng->import_func_count ?
                         eng->import_func_types[func_idx] :
                         eng->funcs[func_idx - eng->import_func_count].type_index))};
                eng->declared_funcs[func_idx] = 1;
                eng->elem_values[segment][item] = slot;
            }
        }
        /* For active segments, the segment's effective type must be a subtype
         * of the table's element type.  Modes 0-3 (bare funcidx) items are
         * non-null function references by construction, so their effective
         * type is (ref func) even though the declared encoding is funcref. */
        wasm_valtype segment_eff_type = ref_type;
        if (!uses_expressions && ref_type == WASM_VALTYPE_FUNCREF)
            segment_eff_type = WASM_VALTYPE_FUNCREF_NONNULL;
        if (active && !global_type_is_compat(
                eng, segment_eff_type,
                eng->tables[table_index]->type_owner,
                eng->tables[table_index]->element_type, 0))
            return exec_fail(err, EXEC_ERROR_FORMAT,
                             "invalid active element segment");
        int apply_segment = active && !eng->instantiation_trapped;
        if (apply_segment &&
            (offset > eng->tables[table_index]->size ||
             item_count > eng->tables[table_index]->size - offset)) {
            eng->instantiation_trapped = 1;
            snprintf(eng->instantiation_error,
                     sizeof(eng->instantiation_error),
                     "out of bounds table access");
            apply_segment = 0;
        }
        if (apply_segment)
            for (uint32_t item = 0; item < item_count; item++)
                eng->tables[table_index]->elements[(size_t)(offset + item)] =
                    eng->elem_values[segment][item];
    }
    return EXEC_OK;
}

/* ---- Code section ---- */


static exec_status parse_body(waste_exec_engine *eng, exec_func *func, wasm_reader *body, exec_error *err) {
    uint32_t local_groups;
    if (!wasm_reader_read_u32(body, &local_groups))
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid local declarations");
    for (uint32_t group = 0; group < local_groups; group++) {
        uint32_t count;
        wasm_valtype value_type;
        if (!wasm_reader_read_u32(body, &count) || !wasm_decode_valtype(body, &value_type) ||
            !value_type_is_defined(eng, value_type) ||
            count > EXEC_MAX_LOCALS - func->local_count)
            return exec_fail(err, EXEC_ERROR_FORMAT, "invalid local declaration");
        for (uint32_t i = 0; i < count; i++) func->locals[func->local_count++] = value_type;
    }

    /* Allocate instruction buffer based on remaining body size */
    size_t capacity = (size_t)(body->end - body->cursor);
    exec_instr *code = (exec_instr *)calloc(capacity + 1, sizeof(*code));
    if (!code)
        return exec_fail(err, EXEC_ERROR_FORMAT, "code alloc failed");

#define FREE_CODE(c, sz) do { \
    for (uint32_t _i = 0; _i < (sz); _i++) { \
        if ((c)[_i].opcode == 0x0e) { \
            uint32_t *_d; memcpy(&_d, (c)[_i].v128_imm.bytes, sizeof(_d)); free(_d); \
        } \
        free((c)[_i].catches); \
    } \
    free(c); \
} while(0)

    uint32_t code_size = 0;
    uint32_t controls[EXEC_MAX_CONTROL];
    uint32_t control_size = 0;
    while (body->cursor < body->end) {
        uint8_t byte;
        if (!wasm_reader_read_u8(body, &byte)) {
            FREE_CODE(code, code_size);
            return exec_fail(err, EXEC_ERROR_FORMAT, "truncated instruction");
        }
        exec_instr instr;
        memset(&instr, 0, sizeof(instr));
        instr.block_type_index = -1;
        if (byte == 0x00 || byte == 0x01) {
            instr.opcode = byte;
        } else if (byte == 0x02 || byte == 0x03 || byte == 0x04 ||
                   byte == 0x1f) {
            uint8_t block_type;
            if (!wasm_reader_read_u8(body, &block_type)) { FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "missing block type"); }
            if (control_size >= EXEC_MAX_CONTROL) {
                FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "control nesting limit exceeded");
            }
            instr.opcode = byte;
            /* A one-byte value type encodes a single-result block. */
            wasm_valtype direct_block_result = WASM_VALTYPE_I32;
            int is_valtype = wasm_decode_byte_valtype(
                block_type, &direct_block_result);
            if (block_type == 0x40) {
                instr.v128_imm.bytes[0] = 0; instr.v128_imm.bytes[1] = 0;
            } else if (is_valtype) {
                instr.v128_imm.bytes[0] = 0; instr.v128_imm.bytes[1] = 1;
                instr.block_result_type = direct_block_result;
                instr.has_block_result_type = 1;
            } else {
                body->cursor--;
                int32_t type_index;
                if (!wasm_reader_read_i32(body, &type_index) || type_index < 0 || (uint32_t)type_index >= eng->type_count) {
                    FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid block type index");
                }
                exec_func_type *block_sig = &eng->types[type_index];
                instr.v128_imm.bytes[0] = (uint8_t)block_sig->param_count;
                instr.v128_imm.bytes[1] = (uint8_t)block_sig->result_count;
                instr.block_type_index = type_index;
            }
            if (byte == 0x1f) {
                uint32_t catch_count;
                if (!wasm_reader_read_u32(body, &catch_count) ||
                    catch_count > WAST_MAX_TAGS) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT,
                                     "invalid try_table catches");
                }
                instr.catches = (exec_catch *)calloc(
                    catch_count ? catch_count : 1, sizeof(*instr.catches));
                if (!instr.catches) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT,
                                     "catch allocation failed");
                }
                instr.catch_count = catch_count;
                for (uint32_t i = 0; i < catch_count; i++) {
                    exec_catch *catch_ = &instr.catches[i];
                    if (!wasm_reader_read_u8(body, &catch_->kind) || catch_->kind > 3 ||
                        (catch_->kind < 2 &&
                         (!wasm_reader_read_u32(body, &catch_->tag_index) ||
                          catch_->tag_index >= eng->tag_count)) ||
                        !wasm_reader_read_u32(body, &catch_->depth)) {
                        free(instr.catches);
                        instr.catches = NULL;
                        FREE_CODE(code, code_size);
                        return exec_fail(err, EXEC_ERROR_FORMAT,
                                         "invalid try_table catch");
                    }
                }
            }
            controls[control_size++] = code_size;
        } else if (byte == 0x08) {
            uint32_t tag_index;
            if (!wasm_reader_read_u32(body, &tag_index) || tag_index >= eng->tag_count) {
                FREE_CODE(code, code_size);
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid throw tag index");
            }
            instr.opcode = byte;
            instr.u32_imm = tag_index;
        } else if (byte == 0x0a) {
            instr.opcode = byte;
        } else if (byte == 0x05) {
            if (!control_size || code[controls[control_size - 1]].opcode != 0x04) {
                FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "else without if");
            }
            instr.opcode = byte;
            code[controls[control_size - 1]].u32_imm = code_size + 1;
        } else if (byte == 0x0c || byte == 0x0d) {
            uint32_t depth;
            if (!wasm_reader_read_u32(body, &depth)) { FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid branch depth"); }
            instr.opcode = byte; instr.u32_imm = depth;
        } else if (byte == 0x0e) {
            /* br_table: u32_imm = count, v128_imm stores uint32_t* to depths[count+1] */
            uint32_t count;
            if (!wasm_reader_read_u32(body, &count) || count > 0xFFFF) { FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid br_table"); }
            uint32_t *depths = (uint32_t *)malloc((count + 1) * sizeof(uint32_t));
            if (!depths) { FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "br_table alloc failed"); }
            for (uint32_t i = 0; i <= count; i++) {
                if (!wasm_reader_read_u32(body, &depths[i])) { free(depths); FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid br_table label"); }
            }
            instr.opcode = byte; instr.u32_imm = count;
            memcpy(instr.v128_imm.bytes, &depths, sizeof(depths)); /* store pointer */
        } else if (byte == 0x10 || byte == 0x12 || byte == 0x14 ||
                   byte == 0x15 || (byte >= 0x20 && byte <= 0x26)) {
            uint32_t idx;
            if (!wasm_reader_read_u32(body, &idx)) {
                FREE_CODE(code, code_size);
                return exec_fail(err, EXEC_ERROR_FORMAT, "truncated instruction immediate");
            }
            instr.opcode  = byte;
            instr.u32_imm = idx;
        } else if (byte == 0x11 || byte == 0x13) {
            uint32_t type_index, table_index;
            if (!wasm_reader_read_u32(body,&type_index) || type_index>=eng->type_count ||
                !wasm_reader_read_u32(body,&table_index) || table_index>=eng->table_count) {
                FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid call_indirect immediate");
            }
            instr.opcode=byte;instr.u32_imm=type_index;instr.simd_op=table_index;
        } else if (byte >= 0x28 && byte <= 0x3e) {
            uint32_t align, memory_index = 0;
            uint64_t offset;
            if (!wasm_reader_read_u32(body, &align) || align >= 0x80u ||
                ((align & 0x40u) && !wasm_reader_read_u32(body, &memory_index)) ||
                !wasm_reader_read_u64(body, &offset)) {
                FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid memory immediate");
            }
            instr.opcode = byte; instr.simd_op = align & 0x3fu;
            instr.u64_imm = offset; instr.memory_index = memory_index;
        } else if (byte == 0x3f || byte == 0x40) {
            uint32_t memory_index;
            if (!wasm_reader_read_u32(body, &memory_index)) {
                FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid memory index");
            }
            instr.opcode = byte; instr.memory_index = memory_index;
        } else if (byte == 0xd0) {
            int32_t heap_type;
            if (!wasm_reader_read_i32(body, &heap_type)) {
                FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "truncated ref.null");
            }
            instr.opcode=byte; instr.u32_imm=(uint32_t)heap_type;
        } else if (byte == 0xd1 || byte == 0xd3 || byte == 0xd4) {
            instr.opcode=byte;
        } else if (byte == 0xd5 || byte == 0xd6) {
            uint32_t depth;
            if (!wasm_reader_read_u32(body, &depth)) {
                FREE_CODE(code, code_size);
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid reference branch depth");
            }
            instr.opcode = byte;
            instr.u32_imm = depth;
        } else if (byte == 0xd2) {
            uint32_t function;
            if (!wasm_reader_read_u32(body, &function)) { FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid ref.func"); }
            instr.opcode=byte; instr.u32_imm=function;
        } else if (byte == 0x1c) {
            uint32_t count;
            wasm_valtype selected_type;
            if (!wasm_reader_read_u32(body, &count) || count != 1 ||
                !wasm_decode_valtype(body, &selected_type) ||
                !value_type_is_defined(eng, selected_type)) {
                FREE_CODE(code, code_size);
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid typed select immediate");
            }
            instr.opcode = 0x1b;
            instr.simd_op = 1;
            memcpy(instr.v128_imm.bytes, &selected_type,
                   sizeof(selected_type));
        } else if (byte == 0x0f || byte == 0x1a || byte == 0x1b) {
            instr.opcode = byte;
        } else if (byte == 0x41) {
            int32_t value;
            if (!wasm_reader_read_i32(body, &value)) {
                FREE_CODE(code, code_size);
                return exec_fail(err, EXEC_ERROR_FORMAT, "invalid i32.const");
            }
            instr.opcode = byte;
            instr.u32_imm = (uint32_t)value;
        } else if (byte == 0x42) {
            int64_t value;
            if (!wasm_reader_read_i64(body, &value)) { FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid i64.const"); }
            instr.opcode = byte; memcpy(instr.v128_imm.bytes, &value, 8);
        } else if (byte == 0x43 || byte == 0x44) {
            size_t width = byte == 0x43 ? 4 : 8; const uint8_t *value;
            if (!wasm_reader_read_bytes(body, width, &value)) { FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "invalid float const"); }
            instr.opcode = byte; memcpy(instr.v128_imm.bytes, value, width);
        } else if ((byte >= 0x45 && byte <= 0x4f) ||   /* i32 cmp */
                   (byte >= 0x50 && byte <= 0x5a) ||   /* i64 cmp */
                   (byte >= 0x5b && byte <= 0x60) ||   /* f32 cmp */
                   (byte >= 0x61 && byte <= 0x66) ||   /* f64 cmp */
                   (byte >= 0x67 && byte <= 0x78) ||   /* i32 arith */
                   (byte >= 0x79 && byte <= 0x8a) ||   /* i64 arith */
                   (byte >= 0x8b && byte <= 0x98) ||   /* f32 arith */
                   (byte >= 0x99 && byte <= 0xa6) ||   /* f64 arith */
                   (byte >= 0xa7 && byte <= 0xbf) ||   /* conversions */
                   byte == 0xc0 || byte == 0xc1 ||     /* i32.extendN_s */
                   (byte >= 0xc2 && byte <= 0xc4)) {   /* i64.extendN_s */
            instr.opcode = byte;
        } else if (byte == 0xFC) {
            /* Saturating trunc (0x00-0x07) and bulk memory ops */
            uint32_t sub_op;
            if (!wasm_reader_read_u32(body, &sub_op)) { FREE_CODE(code, code_size); return exec_fail(err, EXEC_ERROR_FORMAT, "truncated 0xFC op"); }
            instr.opcode  = 0xFC;
            instr.simd_op = sub_op;
            if (sub_op >= 8 && sub_op <= 11) {
                /* memory.init / data.drop / memory.copy / memory.fill */
                uint32_t seg = 0, mem = 0;
                if (sub_op == 10) { /* memory.copy: dst mem, src mem */
                    if (!wasm_reader_read_u32(body,&mem)||!wasm_reader_read_u32(body,&seg)) { FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid memory.copy"); }
                    instr.memory_index = mem;
                    instr.source_memory_index = seg;
                } else if (sub_op == 11) { /* memory.fill: one mem index */
                    if (!wasm_reader_read_u32(body,&mem)) { FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid memory.fill"); }
                    instr.memory_index = mem;
                } else if (sub_op == 8) { /* memory.init: seg, mem */
                    if (!wasm_reader_read_u32(body,&seg)||!wasm_reader_read_u32(body,&mem)) { FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid memory.init"); }
                    eng->uses_data_count_instruction = 1;
                    instr.u32_imm = seg;
                    instr.memory_index = mem;
                } else { /* data.drop: seg */
                    if (!wasm_reader_read_u32(body,&seg)) { FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid data.drop"); }
                    eng->uses_data_count_instruction = 1;
                    instr.u32_imm = seg;
                }
            } else if (sub_op >= 12 && sub_op <= 17) {
                /* table ops: table.init(12), elem.drop(13), table.copy(14),
                   table.grow(15), table.size(16), table.fill(17) */
                uint32_t a, b;
                if (sub_op == 12) { /* table.init: elem idx, table idx */
                    if (!wasm_reader_read_u32(body,&a)||!wasm_reader_read_u32(body,&b)) { FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid table.init"); }
                    instr.u32_imm = a; /* elem segment */
                    instr.v128_imm.bytes[0]=(uint8_t)b; /* table idx */
                } else if (sub_op == 13) { /* elem.drop: elem idx */
                    if (!wasm_reader_read_u32(body,&a)) { FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid elem.drop"); }
                    instr.u32_imm = a;
                } else { /* table.copy, table.grow, table.size, table.fill: one or two table indices */
                    if (sub_op == 14) { /* table.copy: dst, src */
                        if (!wasm_reader_read_u32(body,&a)||!wasm_reader_read_u32(body,&b)) { FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid table.copy"); }
                        instr.u32_imm = a; instr.v128_imm.bytes[0]=(uint8_t)b;
                    } else { /* table.grow/size/fill: table idx */
                        if (!wasm_reader_read_u32(body,&a)) { FREE_CODE(code, code_size); return exec_fail(err,EXEC_ERROR_FORMAT,"invalid table op"); }
                        instr.u32_imm = a;
                    }
                }
            }
            /* sub_op 0x00-0x07 (sat trunc) have no immediates */
        } else if (byte == 0xFB) {
            uint32_t sub_op;
            if (!wasm_reader_read_u32(body, &sub_op) || sub_op > 0x1e) {
                FREE_CODE(code, code_size);
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "invalid GC opcode");
            }
            instr.opcode = 0xFB;
            instr.simd_op = sub_op;
            if (sub_op <= 0x01 || (sub_op >= 0x06 && sub_op <= 0x07) ||
                (sub_op >= 0x0b && sub_op <= 0x0e) ||
                sub_op == 0x10) {
                if (!wasm_reader_read_u32(body, &instr.u32_imm)) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT,
                                     "invalid GC type immediate");
                }
            } else if (sub_op >= 0x02 && sub_op <= 0x05) {
                if (!wasm_reader_read_u32(body, &instr.u32_imm) ||
                    !wasm_reader_read_u32(body, &instr.memory_index)) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT,
                                     "invalid struct field immediate");
                }
            } else if (sub_op == 0x08) {
                if (!wasm_reader_read_u32(body, &instr.u32_imm) ||
                    !wasm_reader_read_u32(body, &instr.lane_index)) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT,
                                     "invalid array.new_fixed immediate");
                }
            } else if (sub_op == 0x09 || sub_op == 0x0a ||
                       sub_op == 0x11 || sub_op == 0x12 || sub_op == 0x13) {
                if (!wasm_reader_read_u32(body, &instr.u32_imm) ||
                    !wasm_reader_read_u32(body, &instr.memory_index)) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT,
                                     "invalid GC dual immediate");
                }
            } else if (sub_op >= 0x14 && sub_op <= 0x17) {
                int32_t heap_type;
                if (!wasm_reader_read_i32(body, &heap_type)) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT,
                                     "invalid GC heap type");
                }
                instr.block_type_index = heap_type;
            } else if (sub_op == 0x18 || sub_op == 0x19) {
                uint8_t flags;
                int32_t source_type, target_type;
                if (!wasm_reader_read_u8(body, &flags) || flags > 3 ||
                    !wasm_reader_read_u32(body, &instr.u32_imm) ||
                    !wasm_reader_read_i32(body, &source_type) ||
                    !wasm_reader_read_i32(body, &target_type)) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT,
                                     "invalid br_on_cast immediate");
                }
                instr.alignment = flags;
                instr.block_type_index = source_type;
                instr.lane_index = (uint32_t)target_type;
            }
        } else if (byte == 0xFD) {
            uint32_t simd_op;
            wast_simd_info simd_info;
            if (!wasm_reader_read_u32(body, &simd_op)) {
                FREE_CODE(code, code_size);
                return exec_fail(err, EXEC_ERROR_FORMAT, "truncated SIMD op");
            }
            instr.opcode  = 0xFD;
            instr.simd_op = simd_op;
            if (!wast_simd_get_info(simd_op, &simd_info)) {
                FREE_CODE(code, code_size);
                return exec_fail(err, EXEC_ERROR_FORMAT, "unknown SIMD opcode");
            }
            if (simd_info.immediate == WAST_SIMD_IMM_CONST ||
                simd_info.immediate == WAST_SIMD_IMM_SHUFFLE) {
                const uint8_t *imm;
                if (!wasm_reader_read_bytes(body, 16, &imm)) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT, "truncated SIMD immediate");
                }
                memcpy(instr.v128_imm.bytes, imm, 16);
                if (simd_info.immediate == WAST_SIMD_IMM_SHUFFLE) {
                    for (uint32_t lane = 0; lane < 16; lane++) {
                        if (instr.v128_imm.bytes[lane] >= 32) {
                            FREE_CODE(code, code_size);
                            return exec_fail(err, EXEC_ERROR_FORMAT,
                                             "SIMD shuffle lane out of range");
                        }
                    }
                }
            } else if (simd_info.immediate == WAST_SIMD_IMM_LANE) {
                const uint8_t *lane;
                if (!wasm_reader_read_bytes(body, 1, &lane) || *lane >= simd_info.lane_count) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT,
                                     "SIMD lane out of range");
                }
                instr.lane_index = *lane;
            } else if (simd_info.immediate == WAST_SIMD_IMM_MEMARG ||
                       simd_info.immediate == WAST_SIMD_IMM_MEMARG_LANE) {
                uint32_t align, memory_index = 0;
                uint64_t offset;
                if (!wasm_reader_read_u32(body, &align)) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT, "invalid SIMD memarg");
                }
                if (align & 0x40u) {
                    align &= ~0x40u;
                    if (!wasm_reader_read_u32(body, &memory_index)) {
                        FREE_CODE(code, code_size);
                        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid SIMD memory index");
                    }
                }
                if (!wasm_reader_read_u64(body, &offset)) {
                    FREE_CODE(code, code_size);
                    return exec_fail(err, EXEC_ERROR_FORMAT, "invalid SIMD offset");
                }
                instr.alignment = align;
                instr.memory_index = memory_index;
                instr.u64_imm = offset;
                if (simd_info.immediate == WAST_SIMD_IMM_MEMARG_LANE) {
                    const uint8_t *lane;
                    if (!wasm_reader_read_bytes(body, 1, &lane) || *lane >= simd_info.lane_count) {
                        FREE_CODE(code, code_size);
                        return exec_fail(err, EXEC_ERROR_FORMAT,
                                         "SIMD lane out of range");
                    }
                    instr.lane_index = *lane;
                }
            }
        } else if (byte == 0x0B) {
            instr.opcode = 0x0B;
            code[code_size++] = instr;
            if (control_size) {
                uint32_t open = controls[--control_size];
                code[open].simd_op = code_size - 1;
                if (code[open].opcode == 0x04) {
                    if (code[open].u32_imm == 0) code[open].u32_imm = code_size - 1;
                    else code[code[open].u32_imm - 1].u32_imm = code_size - 1;
                }
                continue;
            } else break; /* function end */
        } else {
            FREE_CODE(code, code_size);
            return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported opcode");
        }
        if (code_size >= (uint32_t)(capacity + 1)) {
            FREE_CODE(code, code_size);
            return exec_fail(err, EXEC_ERROR_FORMAT, "instruction limit exceeded");
        }
        code[code_size++] = instr;
    }

    wasm_validation_result validation =
        wasm_validate_function(eng, func, code, code_size);
    if (validation.status != WASM_VALIDATION_VALID) {
        FREE_CODE(code, code_size);
        if (err) {
            err->status = validation.status == WASM_VALIDATION_INVALID
                ? EXEC_ERROR_FORMAT : EXEC_ERROR_UNSUPPORTED;
            snprintf(err->message, sizeof(err->message),
                     "%s instruction %u (opcode 0x%x, subopcode 0x%x)",
                     validation.status == WASM_VALIDATION_INVALID
                         ? "invalid function body at"
                         : "unsupported validation for",
                     validation.instruction, validation.opcode,
                     validation.subopcode);
        }
        return validation.status == WASM_VALIDATION_INVALID
            ? EXEC_ERROR_FORMAT : EXEC_ERROR_UNSUPPORTED;
    }
    func->code      = code;
    func->code_size = code_size;
    return EXEC_OK;
}

static exec_status parse_code(waste_exec_engine *eng, wasm_reader *sec, exec_error *err) {
    uint32_t count;
    if (!wasm_reader_read_u32(sec, &count) || count != eng->func_count)
        return exec_fail(err, EXEC_ERROR_FORMAT, "code/function count mismatch");
    for (uint32_t i = 0; i < count; i++) {
        uint32_t body_size;
        wasm_reader body;
        if (!wasm_reader_read_u32(sec, &body_size) ||
            !wasm_reader_read_subreader(sec, body_size, &body))
            return exec_fail(err, EXEC_ERROR_FORMAT, "truncated function body");
        exec_status st = parse_body(eng, &eng->funcs[i], &body, err);
        if (st != EXEC_OK) {
            if (err && err->message[0]) {
                char detail[sizeof(err->message)];
                snprintf(detail, sizeof(detail), "%s", err->message);
                int prefix = snprintf(err->message, sizeof(err->message),
                                      "function %u: ", i);
                size_t out = prefix > 0 ? (size_t)prefix : 0;
                if (out >= sizeof(err->message)) out = sizeof(err->message) - 1;
                for (size_t j = 0; detail[j] && out + 1 < sizeof(err->message);
                     j++)
                    err->message[out++] = detail[j];
                err->message[out] = '\0';
            }
            return st;
        }
    }
    return EXEC_OK;
}

/* ---- Module loader ---- */

exec_status exec_load_decoded_with_imports(
        const uint8_t *bytes, size_t size, const wasm_module *module,
        const exec_imports *imports, waste_exec_engine **eng_out,
        exec_error *err) {
    if (!bytes || !module || module->source != bytes ||
        module->source_size != size || !eng_out)
        return exec_fail(err, EXEC_ERROR_FORMAT, "null input");
    *eng_out = NULL;

    static const uint8_t hdr[8] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
    if (size < 8 || memcmp(bytes, hdr, 8) != 0)
        return exec_fail(err, EXEC_ERROR_FORMAT, "invalid Wasm header");

    waste_exec_engine *eng = (waste_exec_engine *)calloc(1, sizeof(*eng));
    if (!eng)
        return exec_fail(err, EXEC_ERROR_FORMAT, "engine alloc failed");

    wasm_reader r = {
        .start = bytes,
        .cursor = bytes + 8,
        .end = bytes + size,
        .leb_status = WASM_LEB_OK
    };
    uint8_t last_section_rank = 0;
    uint8_t seen_sections[14] = {0};
    uint32_t decoded_section = 0;

    while (module->fully_decoded ? decoded_section < module->section_count :
                                   r.cursor < r.end) {
        uint8_t section_id;
        uint32_t section_size;
        wasm_reader sec;
        if (module->fully_decoded) {
            const wasm_section *section =
                &module->sections[decoded_section++];
            section_id = section->id;
            section_size = section->payload_size;
            sec.start = bytes;
            sec.cursor = bytes + section->payload_offset;
            sec.end = sec.cursor + section_size;
            sec.leb_status = WASM_LEB_OK;
        } else {
            if (!wasm_reader_read_u8(&r, &section_id) ||
                !wasm_reader_read_u32(&r, &section_size) ||
                !wasm_reader_read_subreader(&r, section_size, &sec)) {
                exec_free(eng);
                return exec_fail(err, EXEC_ERROR_FORMAT,
                                 "truncated section");
            }
        }
        if (section_id != 0) {
            if (section_id > 13) {
                exec_free(eng);
                return exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported standard section");
            }
            if (section_id <= 13 && seen_sections[section_id]) {
                exec_free(eng);
                return exec_fail(err, EXEC_ERROR_FORMAT, "duplicate standard section");
            }
            if (section_id <= 13) seen_sections[section_id] = 1;
            uint8_t section_rank = section_id <= 5 ? section_id :
                                   section_id == 13 ? 6 :
                                   section_id <= 9 ? (uint8_t)(section_id + 1) :
                                   section_id == 12 ? 11 :
                                   section_id == 10 ? 12 : 13;
            if (section_rank < last_section_rank) {
                exec_free(eng);
                return exec_fail(err, EXEC_ERROR_FORMAT, "out-of-order section");
            }
            last_section_rank = section_rank;
        }
        exec_status st = EXEC_OK;
        switch (section_id) {
            case 0: { /* custom section: its leading name is UTF-8 */
                uint32_t name_length;
                const uint8_t *name_bytes;
                if (!wasm_reader_read_u32(&sec, &name_length) ||
                    !wasm_reader_read_bytes(&sec, name_length, &name_bytes) ||
                    !valid_utf8(name_bytes, name_length))
                    st = exec_fail(err, EXEC_ERROR_FORMAT,
                                   "malformed UTF-8 encoding");
                else
                    sec.cursor = sec.end;
                break;
            }
            case 1:  st = parse_types(eng, &sec, err);   break;
            case 2:
                sec.cursor = sec.end;
                st = instantiate_imports(eng, module, imports, err);
                break;
            case 3:  st = parse_funcs(eng, &sec, err);   break;
            case 4:  st = parse_tables(eng, &sec, err);  break;
            case 5:  st = parse_memory(eng, &sec, err);  break;
            case 6:  st = parse_globals(eng, &sec, err); break;
            case 7:  st = parse_exports(eng, &sec, err); break;
            case 8:  st = parse_start(eng, &sec, err);   break;
            case 9:  st = parse_elements(eng, &sec, err); break;
            case 10: st = parse_code(eng, &sec, err);    break;
            case 11: st = parse_data(eng, &sec, err);    break;
            case 12:
                if (!wasm_reader_read_u32(&sec, &eng->declared_data_count))
                    st = exec_fail(err, EXEC_ERROR_FORMAT,
                                   "invalid data count section");
                else
                    eng->has_data_count = 1;
                break;
            case 13: st = parse_tags(eng, &sec, err); break;
            default: st = exec_fail(err, EXEC_ERROR_UNSUPPORTED, "unsupported standard section"); break;
        }
        if (st != EXEC_OK) {
            exec_free(eng);
            return st;
        }
        if (section_id != 0 && sec.cursor != sec.end) {
            exec_free(eng);
            return exec_fail(err, EXEC_ERROR_FORMAT, "trailing bytes in section");
        }
    }

    if ((eng->func_count != 0 && !seen_sections[10]) ||
        (eng->uses_data_count_instruction && !eng->has_data_count) ||
        (eng->has_data_count && !seen_sections[11] &&
         eng->declared_data_count != 0)) {
        const char *message = eng->func_count != 0 && !seen_sections[10] ?
            "function and code section count mismatch" :
            "data count section required or inconsistent";
        exec_free(eng);
        return exec_fail(err, EXEC_ERROR_FORMAT, message);
    }

    if (eng->has_start && !eng->instantiation_trapped) {
        wasm_value start_results[WAST_MAX_RESULTS];
        int start_result_count = 0;
        exec_error start_err;
        memset(&start_err, 0, sizeof(start_err));
        exec_status st2 = exec_invoke(eng, eng->start_func, NULL, 0,
                                      start_results, &start_result_count, &start_err);
        if (st2 != EXEC_OK) {
            if (err) *err = start_err;
            if (st2 == EXEC_ERROR_TRAP) {
                /* Instantiation failure does not roll back writes performed
                 * by element/data initialization or by the start function.
                 * Return an unaddressable live instance so references that
                 * escaped through imported tables remain callable. */
                *eng_out = eng;
                return st2;
            }
            exec_free(eng);
            return st2;
        }
    }

    if (eng->instantiation_trapped) {
        *eng_out = eng;
        return exec_fail(err, EXEC_ERROR_TRAP,
                         eng->instantiation_error[0] ?
                         eng->instantiation_error :
                         "module instantiation trapped");
    }

    *eng_out = eng;
    return EXEC_OK;
}

exec_status exec_load_with_imports(const uint8_t *bytes, size_t size,
                                   const exec_imports *imports,
                                   waste_exec_engine **eng_out,
                                   exec_error *err) {
    wasm_module module;
    wasm_decode_error decode_error;
    wasm_decode_status decode_status = wasm_decode_module(
        bytes, size, &module, &decode_error);
    if (decode_status != WASM_DECODE_OK) {
        if (eng_out) *eng_out = NULL;
        if (err) {
            err->status = EXEC_ERROR_FORMAT;
            snprintf(err->message, sizeof(err->message),
                     "%s at byte %zu", decode_error.message,
                     decode_error.offset);
        }
        return EXEC_ERROR_FORMAT;
    }
    exec_status status = wasm_instantiate_module(
        &module, imports, eng_out, err);
    wasm_module_dispose(&module);
    return status;
}

exec_status exec_load(const uint8_t *bytes, size_t size,
                      waste_exec_engine **eng_out, exec_error *err) {
    return exec_load_with_imports(bytes, size, NULL, eng_out, err);
}
