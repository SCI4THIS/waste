/* main.c -- WASTE CLI entry point: WAST spec runner, browser-spec JSON
 * emitter, assertion counter, and parse-only benchmark. */

#include "store.h"
#include "native_wast.h"
#include "native_vfs.h"
#include "wast/runner.h"
#include "runtime_internal.h"
#include "guest_posix.h"
#include "lib/include/kernel.h"
#include "lib/include/select.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *basename_simple(const char *path) {
    const char *last = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') last = p + 1;
    return last;
}

static void json_string(const char *value) {
    native_wast_json_string(stdout, value);
}

static int run_normal(const char *path, const char *vfs_path) {
    FILE *file = fopen(path, "rb");
    long length = -1;
    if (file && !fseek(file, 0, SEEK_END)) length = ftell(file);
    char *source = length >= 0 && (unsigned long)length <= WASTE_VFS_MAX_BYTES ?
                   malloc((size_t)length + 1u) : NULL;
    int ok = source && !fseek(file, 0, SEEK_SET) &&
             fread(source, 1, (size_t)length, file) == (size_t)length;
    if (file) fclose(file);
    if (!ok) {
        fprintf(stderr, "cannot read WAST input: %s\n", path);
        printf("{\"file\":");
        json_string(basename_simple(path));
        printf(",\"error\":\"cannot read WAST input\",\"assertions\":[],\"passed\":0,\"total\":0}\n");
        free(source);
        return 1;
    }
    source[length] = 0;
    native_store store;
    native_store_init(&store);
    if (vfs_path) {
        waste_vfs vfs = {0};
        char error[256];
        int ok = !native_vfs_load(vfs_path, &vfs, error, sizeof(error)) &&
                 !native_store_bind_interpreter_paths(&store) &&
                 !waste_vfs_mount(store.kernel, &vfs) &&
                 !posix_kernel_path_set_cwd(store.kernel, "/root");
        waste_vfs_free(&vfs);
        if (!ok) {
            fprintf(stderr, "cannot mount installed VFS tree: %s\n", vfs_path);
            native_store_free(&store);
            free(source);
            return 1;
        }
    }
    native_wast_bind(&store);
    native_wast_counts counts;
    int result = native_wast_run(&store, basename_simple(path), source,
                                  (size_t)length, &counts);
    native_store_free(&store);
    free(source);
    return result;
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

/* ---- count mode ---- */

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

/* ---- parse-only mode (mmap benchmark) ---- */

static double elapsed_ms(const struct timespec *a, const struct timespec *b) {
    return (double)(b->tv_sec - a->tv_sec) * 1000.0 +
           (double)(b->tv_nsec - a->tv_nsec) / 1000000.0;
}

static int parse_only_file(const char *path, size_t *bytes_out, double *ms_out) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) { perror(path); return 1; }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 0) {
        perror(path); close(fd); return 1;
    }
    size_t length = (size_t)st.st_size;
    void *mapped = NULL;
    if (length != 0) {
        mapped = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped == MAP_FAILED) { perror(path); close(fd); return 1; }
    }
    struct timespec begin, end;
    clock_gettime(CLOCK_MONOTONIC, &begin);
    wast_script *script = (wast_script *)calloc(1, sizeof(*script));
    if (!script) {
        fprintf(stderr, "FAIL %s: out of memory\n", path);
        if (mapped) munmap(mapped, length);
        close(fd);
        return 1;
    }
    int rc = wast_parse_bytes((const char *)(length ? mapped : ""), length, script);
    clock_gettime(CLOCK_MONOTONIC, &end);
    if (mapped) munmap(mapped, length);
    close(fd);
    *bytes_out = length;
    *ms_out = elapsed_ms(&begin, &end);
    if (rc != 0) {
        fprintf(stderr, "FAIL %s: %s\n", path,
                script->error[0] ? script->error : "parse failed");
        wast_script_free(script);
        free(script);
        return 1;
    }
    printf("PASS %s (%zu bytes, %.3f ms, %d commands, %d module group%s, %d assertions)\n",
           path, length, *ms_out, script->command_count, script->group_count,
           script->group_count == 1 ? "" : "s", script->assertion_count);
    wast_script_free(script);
    free(script);
    return 0;
}

static int run_parse_only(int count, char **files) {
    int failures = 0;
    size_t total_bytes = 0;
    double total_ms = 0.0;
    for (int i = 0; i < count; i++) {
        size_t bytes = 0;
        double ms = 0.0;
        failures += parse_only_file(files[i], &bytes, &ms);
        total_bytes += bytes;
        total_ms += ms;
    }
    printf("SUMMARY files=%d failures=%d bytes=%zu parse_ms=%.3f\n",
           count, failures, total_bytes, total_ms);
    return failures ? 1 : 0;
}

/* ---- server mode: one WAST path per stdin line, JSON + sentinel per reply.
 *
 * Amortises the per-test exec/startup cost (static init, dynamic linking,
 * printf buffer setup) across many tests driven by a parent harness.  Each
 * reply ends with "###END###\n" on both stdout and stderr so a parent can
 * delimit per-test output without opening per-test pipes.  run_normal is
 * invoked unchanged, so native_store lifetime, encoding, instantiation, and
 * assertion isolation are identical to one-shot mode.
 */
static int run_server(void) {
    char line[4096];
    for (;;) {
        size_t len = 0;
        int ch;
        while ((ch = getc(stdin)) != -1 && ch != '\n') {
            if (len + 1 < sizeof(line)) line[len++] = (char)ch;
        }
        line[len] = 0;
        while (len > 0 && line[len-1] == '\r') line[--len] = 0;
        if (ch == -1 && len == 0) return 0;
        if (len > 0) run_normal(line, NULL);
        fputs("###END###\n", stdout);
        fputs("###END###\n", stderr);
        if (ch == -1) return 0;
    }
}

/* ---- entry point ---- */

static void usage(FILE *output, const char *name) {
    fprintf(output, "usage: %s [--browser-spec|--count|--parse-only] <file.wast> [...]\n"
                    "       %s --vfs-root <directory> <file.wast>\n"
                    "       %s --server\n", name, name, name);
}

int main(int argc, char *argv[]) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(stdout, argv[0]);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--server") == 0)
        return run_server();
    if (argc >= 3 && strcmp(argv[1], "--parse-only") == 0)
        return run_parse_only(argc - 2, argv + 2);
    if (argc == 3 && strcmp(argv[1], "--browser-spec") == 0)
        return run_browser_spec(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--count") == 0)
        return run_count(argv[2]);
    if (argc == 2)
        return run_normal(argv[1], NULL);
    if (argc == 4 && strcmp(argv[1], "--vfs-root") == 0)
        return run_normal(argv[3], argv[2]);
    usage(stderr, argv[0]);
    return 1;
}
