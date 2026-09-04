#include "wast_encode.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- LEB128 helpers ---- */

/* Return number of bytes needed to encode val as unsigned LEB128 */
static size_t leb128_u32_size(uint32_t val) {
    size_t n = 1;
    while (val >= 0x80u) { val >>= 7; n++; }
    return n;
}

/* Emit unsigned LEB128 */
static void leb128_u32(uint8_t *buf, size_t *pos, uint32_t val) {
    do {
        uint8_t byte = (uint8_t)(val & 0x7Fu);
        val >>= 7;
        if (val != 0) byte |= 0x80u;
        buf[(*pos)++] = byte;
    } while (val != 0);
}

/* ---- Type byte for valtype ---- */
static uint8_t valtype_byte(wasm_valtype t) {
    switch (t) {
        case WASM_VALTYPE_I32:  return 0x7F;
        case WASM_VALTYPE_I64:  return 0x7E;
        case WASM_VALTYPE_F32:  return 0x7D;
        case WASM_VALTYPE_F64:  return 0x7C;
        case WASM_VALTYPE_V128: return 0x7B;
        default:                return 0x7F;
    }
}

/* ---- Function type signature ---- */
typedef struct {
    wasm_valtype params[WAST_MAX_PARAMS];
    int          param_count;
    wasm_valtype result;
    int          has_result;
} func_sig;

static int sig_equal(const func_sig *a, const func_sig *b) {
    if (a->param_count != b->param_count) return 0;
    if (a->has_result != b->has_result) return 0;
    if (a->has_result && a->result != b->result) return 0;
    for (int i = 0; i < a->param_count; i++)
        if (a->params[i] != b->params[i]) return 0;
    return 1;
}

/* ---- Instruction binary size (not counting the instruction, just immediates) ---- */

/* Return byte size of a single instruction in binary form */
static size_t instr_binary_size(const wasm_instr *instr) {
    if (instr->opcode == 0x20) {
        /* local.get: 0x20 + LEB128(u32_imm) */
        return 1 + leb128_u32_size(instr->u32_imm);
    }
    if (instr->opcode == 0xFD) {
        /* 0xFD + LEB128(simd_op) + possible immediate */
        size_t base = 1 + leb128_u32_size(instr->simd_op);
        if (instr->simd_op == 12) {
            /* v128.const: 16 bytes of immediate */
            base += 16;
        }
        return base;
    }
    if (instr->opcode == 0x0B) {
        return 1; /* end */
    }
    return 1; /* fallback */
}

/* Return the binary size of one function body (local decls byte + instructions) */
static size_t func_body_size(const wast_func *func) {
    /* 0x00 = local decl count 0 */
    size_t sz = 1;
    for (int i = 0; i < func->instr_count; i++)
        sz += instr_binary_size(&func->instrs[i]);
    return sz;
}

/* ---- Type section size calculation ---- */

static size_t type_entry_size(const func_sig *sig) {
    /* 0x60 + param_count_leb + param_types + result_count_leb + result_types */
    size_t sz = 1; /* 0x60 */
    sz += leb128_u32_size((uint32_t)sig->param_count);
    sz += (size_t)sig->param_count; /* one byte per param type */
    sz += 1; /* result count (0 or 1) */
    if (sig->has_result) sz += 1; /* result type byte */
    return sz;
}

/* ---- Emit an instruction ---- */
static void emit_instr(uint8_t *buf, size_t *pos, const wasm_instr *instr) {
    if (instr->opcode == 0x20) {
        buf[(*pos)++] = 0x20;
        leb128_u32(buf, pos, instr->u32_imm);
    } else if (instr->opcode == 0xFD) {
        buf[(*pos)++] = 0xFD;
        leb128_u32(buf, pos, instr->simd_op);
        if (instr->simd_op == 12) {
            memcpy(buf + *pos, instr->v128_imm.bytes, 16);
            *pos += 16;
        }
    } else if (instr->opcode == 0x0B) {
        buf[(*pos)++] = 0x0B;
    } else {
        buf[(*pos)++] = (uint8_t)instr->opcode;
    }
}

/* ---- Main encoder ---- */

uint8_t *wast_encode_module(const wast_module *module, size_t *size_out, char *error) {
    int func_count = module->func_count;
    if (func_count == 0) {
        /* Empty module with all sections missing */
        /* Just emit a minimal valid module with no functions */
        uint8_t *buf = (uint8_t *)malloc(8);
        if (!buf) {
            if (error) snprintf(error, 256, "allocation failed");
            return NULL;
        }
        buf[0] = 0x00; buf[1] = 0x61; buf[2] = 0x73; buf[3] = 0x6D;
        buf[4] = 0x01; buf[5] = 0x00; buf[6] = 0x00; buf[7] = 0x00;
        *size_out = 8;
        return buf;
    }

    /* --- Pass 1: Collect unique type signatures --- */
    func_sig sigs[WAST_MAX_FUNCS];
    int      type_index[WAST_MAX_FUNCS]; /* type index for each function */
    int      sig_count = 0;

    for (int i = 0; i < func_count; i++) {
        const wast_func *f = &module->funcs[i];
        func_sig s;
        memset(&s, 0, sizeof(s));
        s.param_count = f->param_count;
        s.has_result  = f->has_result;
        s.result      = f->result;
        for (int p = 0; p < f->param_count; p++) s.params[p] = f->params[p];

        /* Find existing sig */
        int found = -1;
        for (int j = 0; j < sig_count; j++) {
            if (sig_equal(&sigs[j], &s)) { found = j; break; }
        }
        if (found < 0) {
            if (sig_count >= WAST_MAX_FUNCS) {
                if (error) snprintf(error, 256, "too many distinct type signatures");
                return NULL;
            }
            sigs[sig_count] = s;
            found = sig_count++;
        }
        type_index[i] = found;
    }

    /* --- Pass 1: Compute section sizes --- */

    /* Type section content size */
    size_t type_content = leb128_u32_size((uint32_t)sig_count);
    for (int i = 0; i < sig_count; i++)
        type_content += type_entry_size(&sigs[i]);

    /* Function section content size: count + type indices */
    size_t func_content = leb128_u32_size((uint32_t)func_count);
    for (int i = 0; i < func_count; i++)
        func_content += leb128_u32_size((uint32_t)type_index[i]);

    /* Export section content size */
    size_t export_content = leb128_u32_size((uint32_t)func_count);
    for (int i = 0; i < func_count; i++) {
        size_t name_len = strlen(module->funcs[i].export_name);
        export_content += leb128_u32_size((uint32_t)name_len);
        export_content += name_len;
        export_content += 1; /* export kind: 0x00 = function */
        export_content += leb128_u32_size((uint32_t)i);
    }

    /* Code section content size */
    size_t body_sizes[WAST_MAX_FUNCS];
    size_t code_content = leb128_u32_size((uint32_t)func_count);
    for (int i = 0; i < func_count; i++) {
        body_sizes[i] = func_body_size(&module->funcs[i]);
        /* Each entry is: LEB128(body_size) + body_size bytes */
        code_content += leb128_u32_size((uint32_t)body_sizes[i]);
        code_content += body_sizes[i];
    }

    /* Total binary size:
       magic(4) + version(4)
       + section_id(1) + section_size_leb + section_content  (for each section)
    */
    size_t total = 8;
    total += 1 + leb128_u32_size((uint32_t)type_content)   + type_content;
    total += 1 + leb128_u32_size((uint32_t)func_content)   + func_content;
    total += 1 + leb128_u32_size((uint32_t)export_content) + export_content;
    total += 1 + leb128_u32_size((uint32_t)code_content)   + code_content;

    /* --- Pass 2: Emit binary --- */
    uint8_t *buf = (uint8_t *)calloc(1, total);
    if (!buf) {
        if (error) snprintf(error, 256, "allocation failed for binary module");
        return NULL;
    }

    size_t pos = 0;

    /* Magic + version */
    buf[pos++] = 0x00; buf[pos++] = 0x61; buf[pos++] = 0x73; buf[pos++] = 0x6D;
    buf[pos++] = 0x01; buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x00;

    /* Section 1: Type */
    buf[pos++] = 0x01; /* section id */
    leb128_u32(buf, &pos, (uint32_t)type_content);
    leb128_u32(buf, &pos, (uint32_t)sig_count);
    for (int i = 0; i < sig_count; i++) {
        const func_sig *s = &sigs[i];
        buf[pos++] = 0x60; /* func type marker */
        leb128_u32(buf, &pos, (uint32_t)s->param_count);
        for (int p = 0; p < s->param_count; p++)
            buf[pos++] = valtype_byte(s->params[p]);
        if (s->has_result) {
            buf[pos++] = 0x01; /* one result */
            buf[pos++] = valtype_byte(s->result);
        } else {
            buf[pos++] = 0x00; /* no results */
        }
    }

    /* Section 3: Function */
    buf[pos++] = 0x03;
    leb128_u32(buf, &pos, (uint32_t)func_content);
    leb128_u32(buf, &pos, (uint32_t)func_count);
    for (int i = 0; i < func_count; i++)
        leb128_u32(buf, &pos, (uint32_t)type_index[i]);

    /* Section 7: Export */
    buf[pos++] = 0x07;
    leb128_u32(buf, &pos, (uint32_t)export_content);
    leb128_u32(buf, &pos, (uint32_t)func_count);
    for (int i = 0; i < func_count; i++) {
        const char *name = module->funcs[i].export_name;
        uint32_t name_len = (uint32_t)strlen(name);
        leb128_u32(buf, &pos, name_len);
        memcpy(buf + pos, name, name_len);
        pos += name_len;
        buf[pos++] = 0x00; /* function export */
        leb128_u32(buf, &pos, (uint32_t)i);
    }

    /* Section 10: Code */
    buf[pos++] = 0x0A;
    leb128_u32(buf, &pos, (uint32_t)code_content);
    leb128_u32(buf, &pos, (uint32_t)func_count);
    for (int i = 0; i < func_count; i++) {
        const wast_func *f = &module->funcs[i];
        leb128_u32(buf, &pos, (uint32_t)body_sizes[i]);
        /* local decl count = 0 */
        buf[pos++] = 0x00;
        for (int j = 0; j < f->instr_count; j++)
            emit_instr(buf, &pos, &f->instrs[j]);
    }

    if (pos != total) {
        free(buf);
        if (error) snprintf(error, 256, "encoder size mismatch: expected %zu got %zu", total, pos);
        return NULL;
    }

    *size_out = total;
    return buf;
}
