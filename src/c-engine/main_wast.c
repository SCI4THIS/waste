#include "wast_types.h"
#include "wast_encode.h"
#include "waste_exec.h"
#include "wast_runner.h"
#include "wast_general.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *basename_simple(const char *path) {
    const char *last = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') last = p + 1;
    return last;
}

static void free_script(wast_script *script) {
    if (!script) return;
    for (int g = 0; g < script->group_count; g++) {
        for (int d = 0; d < script->groups[g].module.data_count; d++)
            free(script->groups[g].module.data[d].bytes);
        free(script->groups[g].module.funcs);
    }
    free(script);
}

/* Emit a JSON string with escaping */
static void json_string(const char *s) {
    putchar('"');
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"') fputs("\\\"", stdout);
        else if (ch == '\\') fputs("\\\\", stdout);
        else if (ch == '\n') fputs("\\n", stdout);
        else if (ch == '\r') fputs("\\r", stdout);
        else if (ch == '\t') fputs("\\t", stdout);
        else if (ch < 0x20 || ch >= 0x80) printf("\\u%04x", (unsigned)ch);
        else putchar((int)ch);
    }
    putchar('"');
}

/* ---- normal run mode ---- */

static int run_normal(const char *path) {
    const char *filename = basename_simple(path);

    wast_script *script = (wast_script *)calloc(1, sizeof(*script));
    if (!script) return 1;
    int parse_rc = wast_parse_file(path, script);
    if (parse_rc != 0) {
        fprintf(stderr, "parse error in %s: %s\n", path, script->error);
        printf("{\"file\":");
        json_string(filename);
        printf(",\"error\":");
        json_string(script->error);
        printf(",\"assertions\":[],\"passed\":0,\"total\":0}\n");
        free_script(script); return 1;
    }

    int total_passed = 0;
    int total_count  = 0;
    int first_assertion = 1;

    printf("{\"file\":");
    json_string(filename);
    printf(",\"assertions\":[\n");

    for (int g = 0; g < script->group_count; g++) {
        wast_group *group = &script->groups[g];

        char encode_error[256] = {0};
        size_t bin_size = 0;
        uint8_t *bin = wast_encode_module(&group->module, &bin_size, encode_error);
        if (!bin) {
            fprintf(stderr, "encode error (group %d): %s\n", g, encode_error);
            for (int i = 0; i < group->assertion_count; i++) {
                if (!first_assertion) printf(",\n");
                first_assertion = 0;
                printf("{\"index\":%d,\"func\":", total_count + i);
                json_string(script->assertions[group->assertion_start + i].func_name);
                printf(",\"pass\":false,\"error\":");
                json_string(encode_error);
                printf("}");
            }
            total_count += group->assertion_count;
            continue;
        }

        waste_exec_engine *engine = NULL;
        exec_error exec_err;
        memset(&exec_err, 0, sizeof(exec_err));
        exec_status st = exec_load(bin, bin_size, &engine, &exec_err);
        free(bin);
        if (st != EXEC_OK) {
            fprintf(stderr, "load error (group %d): %s\n", g, exec_err.message);
            for (int i = 0; i < group->assertion_count; i++) {
                if (!first_assertion) printf(",\n");
                first_assertion = 0;
                printf("{\"index\":%d,\"func\":", total_count + i);
                json_string(script->assertions[group->assertion_start + i].func_name);
                printf(",\"pass\":false,\"error\":");
                json_string(exec_err.message);
                printf("}");
            }
            total_count += group->assertion_count;
            continue;
        }

        for (int i = 0; i < group->assertion_count; i++) {
            const wast_assertion *a = &script->assertions[group->assertion_start + i];
            exec_error aerr;
            memset(&aerr, 0, sizeof(aerr));
            exec_status ast = wast_run_assertion(engine, a, &aerr);
            int ok = (ast == EXEC_OK);
            if (ok) total_passed++;

            if (!first_assertion) printf(",\n");
            first_assertion = 0;

            printf("{\"index\":%d,\"func\":", total_count + i);
            json_string(a->func_name);
            printf(",\"pass\":%s,\"error\":", ok ? "true" : "false");
            if (ok || aerr.message[0] == '\0') {
                printf("null");
            } else {
                json_string(aerr.message);
            }
            printf("}");
        }
        total_count += group->assertion_count;
        exec_free(engine);
    }

    printf("\n],\"passed\":%d,\"total\":%d}\n", total_passed, total_count);
    free_script(script);
    return (total_passed == total_count) ? 0 : 1;
}

/* ---- browser-spec mode ---- */

/*
 * Emit a single wasm_value as a JSON object:
 * {"type":N,"data":[0,...,0],"nan_mode":[0,...,0]}
 * data is always 16 bytes (little-endian for scalars, raw for v128).
 */
static void json_value_spec(const wasm_value *v) {
    uint8_t data[16] = {0};
    switch (v->type) {
        case WASM_VALTYPE_I32: {
            uint32_t tmp; memcpy(&tmp, &v->i32, 4);
            data[0]=(uint8_t)(tmp);     data[1]=(uint8_t)(tmp>>8);
            data[2]=(uint8_t)(tmp>>16); data[3]=(uint8_t)(tmp>>24);
            break;
        }
        case WASM_VALTYPE_I64: {
            uint64_t tmp; memcpy(&tmp, &v->i64, 8);
            for (int k = 0; k < 8; k++) data[k] = (uint8_t)(tmp >> (k*8));
            break;
        }
        case WASM_VALTYPE_F32: {
            uint32_t tmp; memcpy(&tmp, &v->f32, 4);
            data[0]=(uint8_t)(tmp);     data[1]=(uint8_t)(tmp>>8);
            data[2]=(uint8_t)(tmp>>16); data[3]=(uint8_t)(tmp>>24);
            break;
        }
        case WASM_VALTYPE_F64: {
            uint64_t tmp; memcpy(&tmp, &v->f64, 8);
            for (int k = 0; k < 8; k++) data[k] = (uint8_t)(tmp >> (k*8));
            break;
        }
        case WASM_VALTYPE_V128:
            memcpy(data, v->v128.bytes, 16);
            break;
        case WASM_VALTYPE_FUNCREF:
        case WASM_VALTYPE_EXTERNREF:
        case WASM_VALTYPE_FUNCREF_NONNULL:
        case WASM_VALTYPE_EXTERNREF_NONNULL: {
            uint32_t tmp = v->ref;
            data[0]=(uint8_t)tmp; data[1]=(uint8_t)(tmp>>8);
            data[2]=(uint8_t)(tmp>>16); data[3]=(uint8_t)(tmp>>24);
            break;
        }
    }

    printf("{\"type\":%d,\"data\":[", (int)v->type);
    for (int i = 0; i < 16; i++) {
        if (i) putchar(',');
        printf("%d", (int)data[i]);
    }
    printf("],\"nan_mode\":[");
    for (int i = 0; i < 16; i++) {
        if (i) putchar(',');
        printf("%d", (int)v->nan_mode[i]);
    }
    printf("]}");
}

static int run_browser_spec(const char *path) {
    const char *filename = basename_simple(path);

    wast_script *script = (wast_script *)calloc(1, sizeof(*script));
    if (!script) return 1;
    int parse_rc = wast_parse_file(path, script);
    if (parse_rc != 0) {
        fprintf(stderr, "parse error in %s: %s\n", path, script->error);
        printf("{\"file\":");
        json_string(filename);
        printf(",\"error\":");
        json_string(script->error);
        printf(",\"groups\":[]}\n");
        free_script(script); return 1;
    }

    printf("{\"file\":");
    json_string(filename);
    printf(",\"groups\":[\n");

    for (int g = 0; g < script->group_count; g++) {
        if (g) printf(",\n");
        wast_group *group = &script->groups[g];

        char encode_error[256] = {0};
        size_t bin_size = 0;
        uint8_t *bin = wast_encode_module(&group->module, &bin_size, encode_error);

        printf("{\"id\":");
        json_string(group->module.id);
        printf(",\"register\":");
        json_string(group->module.register_name);
        printf(",\"module_assertion\":");
        if (group->has_module_assertion) {
            printf("{\"kind\":%d,\"expected\":", (int)group->module_assert_kind);
            json_string(group->expected_module_error);
            putchar('}');
        } else {
            printf("null");
        }
        printf(",\"module_hex\":\"");
        if (bin) {
            for (size_t i = 0; i < bin_size; i++) printf("%02x", bin[i]);
            free(bin);
        }
        printf("\",\"assertions\":[\n");

        for (int i = 0; i < group->assertion_count; i++) {
            const wast_assertion *a = &script->assertions[group->assertion_start + i];
            if (i) printf(",\n");
            printf("{\"func\":");
            json_string(a->func_name);
            printf(",\"action\":");
            json_string(a->action_kind == WAST_ACTION_GET ? "get" : "invoke");
            printf(",\"kind\":%d", (int)a->kind);
            printf(",\"module\":");
            json_string(a->module_id);
            printf(",\"args\":[");
            for (int j = 0; j < a->arg_count; j++) {
                if (j) putchar(',');
                json_value_spec(&a->args[j]);
            }
            printf("],\"alts\":[");
            for (int alt = 0; alt < a->alt_count; alt++) {
                if (alt) putchar(',');
                putchar('[');
                for (int r = 0; r < a->result_count; r++) {
                    if (r) putchar(',');
                    json_value_spec(&a->alternatives[alt][r]);
                }
                putchar(']');
            }
            printf("]}");
        }
        printf("\n]}");
    }

    printf("\n]}\n");
    free_script(script);
    return 0;
}

/* ---- detect whether a file needs the general interpreter ----
   Files with SIMD keywords go through the flex/bison path;
   everything else goes through the general WAT interpreter.      */
static int needs_general(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return 0; }
    size_t n = (size_t)sz;
    char *buf = (char *)malloc(n + 1);
    if (!buf) { fclose(f); return 0; }
    fread(buf, 1, n, f); fclose(f);
    buf[n] = '\0';
    /* SIMD markers: if present, use flex/bison path (return 0) */
    static const char *simd_markers[] = {
        "v128", "i8x16", "i16x8", "i32x4", "i64x2", "f32x4", "f64x2",
        "relaxed_swizzle",
        NULL
    };
    for (int i = 0; simd_markers[i]; i++) {
        const char *m = simd_markers[i];
        size_t mlen = strlen(m);
        for (size_t j = 0; j + mlen <= n; j++) {
            if (memcmp(buf + j, m, mlen) == 0) { free(buf); return 0; }
        }
    }
    free(buf);
    return 1;
}

/* ---- entry point ---- */

int main(int argc, char *argv[]) {
    if (argc == 3 && strcmp(argv[1], "--browser-spec") == 0)
        return run_browser_spec(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--general") == 0)
        return wast_general_run(argv[2]);
    if (argc == 2) {
        if (needs_general(argv[1]))
            return wast_general_run(argv[1]);
        return run_normal(argv[1]);
    }
    fprintf(stderr, "usage: %s [--browser-spec|--general] <file.wast>\n", argv[0]);
    return 1;
}
