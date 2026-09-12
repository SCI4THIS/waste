#include "wast_linker.h"
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
        wast_script_free(script); free(script); return 1;
    }

    int total_passed = 0;
    int total_count  = 0;
    int first_assertion = 1;
    native_store store;
    native_store_init(&store);

    printf("{\"file\":");
    json_string(filename);
    printf(",\"assertions\":[\n");

    for (int g = 0; g < script->group_count; g++) {
        wast_group *group = &script->groups[g];

        /* Definitions are templates.  A module instance below encodes and
         * instantiates the definition afresh, which gives its globals,
         * tables, memories, and tags distinct identities. */
        if (group->module.is_definition) continue;

        const wast_module *load_module = &group->module;
        if (group->module.instance_of[0]) {
            load_module = native_find_definition(
                script, g, group->module.instance_of);
            if (!load_module) {
                fprintf(stderr, "unknown module definition %s\n",
                        group->module.instance_of);
                continue;
            }
        }

        if (group->has_module_assertion && group->has_validation_error) {
            int ok = group->module_assert_kind == WAST_ASSERT_INVALID ||
                     group->module_assert_kind == WAST_ASSERT_MALFORMED;
            if (!first_assertion) printf(",\n");
            first_assertion = 0;
            printf("{\"index\":%d,\"func\":\"(module)\",\"pass\":%s,\"error\":",
                   total_count, ok ? "true" : "false");
            if (ok) printf("null"); else json_string(group->validation_error);
            printf("}");
            total_count++;
            if (ok) total_passed++;
            continue;
        }

        char encode_error[256] = {0};
        size_t bin_size = 0;
        wast_group encode_group = *group;
        encode_group.module = *load_module;
        uint8_t *bin = encode_group_module(&encode_group, &bin_size,
                                           encode_error);
        if (!bin) {
            fprintf(stderr, "encode error (group %d): %s\n", g, encode_error);
            if (group->has_module_assertion) {
                int ok = group->module_assert_kind != WAST_ASSERT_TRAP;
                if (!first_assertion) printf(",\n");
                first_assertion = 0;
                printf("{\"index\":%d,\"func\":\"(module)\",\"pass\":%s,\"error\":",
                       total_count, ok ? "true" : "false");
                if (ok) printf("null"); else json_string(encode_error);
                printf("}");
                total_count++;
                if (ok) total_passed++;
                continue;
            }
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
        exec_status st = native_load_module(&store, load_module,
                                            bin, bin_size, &engine, &exec_err);
        free(bin);
        if (group->has_module_assertion) {
            int ok = group->module_assert_kind == WAST_ASSERT_TRAP ?
                st == EXEC_ERROR_TRAP : st != EXEC_OK;
            if (!first_assertion) printf(",\n");
            first_assertion = 0;
            printf("{\"index\":%d,\"func\":\"(module)\",\"pass\":%s,\"error\":",
                   total_count, ok ? "true" : "false");
            if (ok) printf("null");
            else json_string(st == EXEC_OK ? "module unexpectedly instantiated" :
                             exec_err.message);
            printf("}");
            total_count++;
            if (ok) total_passed++;
            if (engine && !native_store_keep_orphan(&store, engine)) {
                /* Preserve any funcrefs installed into imported tables even
                 * if the lifetime bookkeeping itself cannot grow. */
            }
            continue;
        }
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

        if (!native_store_add(&store, engine, &group->module, load_module)) {
            exec_free(engine);
            engine = NULL;
            snprintf(exec_err.message, sizeof(exec_err.message),
                     "out of memory retaining module instance");
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
            waste_exec_engine *selected = native_selected_engine(&store,
                                                                  a->module_id);
            exec_error aerr;
            memset(&aerr, 0, sizeof(aerr));
            exec_status ast;
            if (!selected) {
                aerr.status = EXEC_ERROR_NOT_FOUND;
                snprintf(aerr.message, sizeof(aerr.message),
                         "unknown module id");
                ast = EXEC_ERROR_NOT_FOUND;
            } else {
                ast = wast_run_assertion(selected, a, &aerr);
            }
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
    }

    printf("\n],\"passed\":%d,\"total\":%d}\n", total_passed, total_count);
    native_store_free(&store);
    wast_script_free(script); free(script);
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
        case WASM_VALTYPE_EXTERNREF_NONNULL:
        case WASM_VALTYPE_ANYREF:
        case WASM_VALTYPE_EQREF:
        case WASM_VALTYPE_I31REF:
        case WASM_VALTYPE_STRUCTREF:
        case WASM_VALTYPE_ARRAYREF:
        case WASM_VALTYPE_ANYREF_NONNULL:
        case WASM_VALTYPE_EQREF_NONNULL:
        case WASM_VALTYPE_I31REF_NONNULL:
        case WASM_VALTYPE_STRUCTREF_NONNULL:
        case WASM_VALTYPE_ARRAYREF_NONNULL:
        case WASM_VALTYPE_EXNREF:
        case WASM_VALTYPE_EXNREF_NONNULL:
        case WASM_VALTYPE_NULLREF:
        case WASM_VALTYPE_NULLFUNCREF:
        case WASM_VALTYPE_NULLEXNREF:
        case WASM_VALTYPE_NULLEXTERNREF: {
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
    if (parse_rc != 0 && script->group_count == 0) {
        fprintf(stderr, "parse error in %s: %s\n", path, script->error);
        printf("{\"file\":");
        json_string(filename);
        printf(",\"error\":");
        json_string(script->error);
        printf(",\"groups\":[]}\n");
        wast_script_free(script); free(script); return 1;
    }
    if (parse_rc != 0) {
        fprintf(stderr, "partial parse of %s: %s (emitting %d groups)\n",
                path, script->error, script->group_count);
    }

    printf("{\"file\":");
    json_string(filename);
    printf(",\"groups\":[\n");

    for (int g = 0; g < script->group_count; g++) {
        if (g) printf(",\n");
        wast_group *group = &script->groups[g];

        char encode_error[256] = {0};
        size_t bin_size = 0;
        uint8_t *bin = encode_group_module(group, &bin_size, encode_error);

        printf("{\"id\":");
        json_string(group->module.id);
        printf(",\"register\":");
        json_string(group->module.register_name);
        printf(",\"module_assertion\":");
        if (group->has_module_assertion) {
            printf("{\"kind\":%d,\"expected\":", (int)group->module_assert_kind);
            json_string(group->expected_module_error);
            printf(",\"validation_error\":");
            if (group->has_validation_error) json_string(group->validation_error);
            else printf("null");
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
    wast_script_free(script); free(script);
    return 0;
}

/* ---- detect whether a file needs the general interpreter ----
   Files with SIMD keywords go through the flex/bison path;
   everything else goes through the general WAT interpreter.      */

/* ---- entry point ---- */

static int run_count(const char *path) {
    wast_script *script = (wast_script *)calloc(1, sizeof(*script));
    if (!script) return 1;
    int parse_rc = wast_parse_file(path, script);
    if (parse_rc != 0 && script->group_count == 0) {
        fprintf(stderr, "parse error in %s: %s\n", path, script->error);
        wast_script_free(script); free(script); return 1;
    }
    int count = 0;
    for (int group = 0; group < script->group_count; group++)
        count += script->groups[group].assertion_count;
    printf("%d\n", count);
    wast_script_free(script); free(script);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc == 3 && strcmp(argv[1], "--browser-spec") == 0)
        return run_browser_spec(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--count") == 0)
        return run_count(argv[2]);
    if (argc == 2) {
        return run_normal(argv[1]);
    }
    fprintf(stderr, "usage: %s [--browser-spec|--count] <file.wast>\n", argv[0]);
    return 1;
}
