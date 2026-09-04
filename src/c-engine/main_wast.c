#include "wast_types.h"
#include "wast_encode.h"
#include "waste_exec.h"
#include "wast_runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *basename_simple(const char *path) {
    const char *last = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') last = p + 1;
    return last;
}

/* Emit a JSON string with escaping */
static void json_string(const char *s) {
    putchar('"');
    for (; *s; s++) {
        if (*s == '"') fputs("\\\"", stdout);
        else if (*s == '\\') fputs("\\\\", stdout);
        else if (*s == '\n') fputs("\\n", stdout);
        else putchar(*s);
    }
    putchar('"');
}

/* ---- normal run mode ---- */

static int run_normal(const char *path) {
    const char *filename = basename_simple(path);

    wast_script script;
    int parse_rc = wast_parse_file(path, &script);
    if (parse_rc != 0) {
        fprintf(stderr, "parse error in %s: %s\n", path, script.error);
        printf("{\"file\":");
        json_string(filename);
        printf(",\"error\":");
        json_string(script.error);
        printf(",\"assertions\":[],\"passed\":0,\"total\":0}\n");
        return 1;
    }

    int total_passed = 0;
    int total_count  = 0;
    int first_assertion = 1;

    printf("{\"file\":");
    json_string(filename);
    printf(",\"assertions\":[\n");

    for (int g = 0; g < script.group_count; g++) {
        wast_group *group = &script.groups[g];

        char encode_error[256] = {0};
        size_t bin_size = 0;
        uint8_t *bin = wast_encode_module(&group->module, &bin_size, encode_error);
        if (!bin) {
            fprintf(stderr, "encode error (group %d): %s\n", g, encode_error);
            for (int i = 0; i < group->assertion_count; i++) {
                if (!first_assertion) printf(",\n");
                first_assertion = 0;
                printf("{\"index\":%d,\"func\":", total_count + i);
                json_string(group->assertions[i].func_name);
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
                json_string(group->assertions[i].func_name);
                printf(",\"pass\":false,\"error\":");
                json_string(exec_err.message);
                printf("}");
            }
            total_count += group->assertion_count;
            continue;
        }

        for (int i = 0; i < group->assertion_count; i++) {
            const wast_assertion *a = &group->assertions[i];
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

    wast_script script;
    int parse_rc = wast_parse_file(path, &script);
    if (parse_rc != 0) {
        fprintf(stderr, "parse error in %s: %s\n", path, script.error);
        printf("{\"file\":");
        json_string(filename);
        printf(",\"error\":");
        json_string(script.error);
        printf(",\"groups\":[]}\n");
        return 1;
    }

    printf("{\"file\":");
    json_string(filename);
    printf(",\"groups\":[\n");

    for (int g = 0; g < script.group_count; g++) {
        if (g) printf(",\n");
        wast_group *group = &script.groups[g];

        char encode_error[256] = {0};
        size_t bin_size = 0;
        uint8_t *bin = wast_encode_module(&group->module, &bin_size, encode_error);

        printf("{\"module_hex\":\"");
        if (bin) {
            for (size_t i = 0; i < bin_size; i++) printf("%02x", bin[i]);
            free(bin);
        }
        printf("\",\"assertions\":[\n");

        for (int i = 0; i < group->assertion_count; i++) {
            const wast_assertion *a = &group->assertions[i];
            if (i) printf(",\n");
            printf("{\"func\":");
            json_string(a->func_name);
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
    return 0;
}

/* ---- entry point ---- */

int main(int argc, char *argv[]) {
    if (argc == 3 && strcmp(argv[1], "--browser-spec") == 0)
        return run_browser_spec(argv[2]);
    if (argc == 2)
        return run_normal(argv[1]);
    fprintf(stderr, "usage: %s [--browser-spec] <file.wast>\n", argv[0]);
    return 1;
}
