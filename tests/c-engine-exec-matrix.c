#include "store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int waste_wat_compile(const char *bytes, size_t length, uint8_t **wasm_out,
                      size_t *wasm_length, char *error,
                      size_t error_length) {
    (void)bytes; (void)length; (void)wasm_out; (void)wasm_length;
    if (error && error_length) snprintf(error, error_length, "parser stub");
    return -1;
}

uint8_t *wast_encode_module(const wast_module *module, size_t *size,
                            char *error) {
    (void)module; (void)size; (void)error;
    return NULL;
}

static int checks;
static int failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { fprintf(stderr, "FAIL: "); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

static uint8_t *read_all(const char *path, size_t *size_out) {
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *bytes;
    if (!file || fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        if (file) fclose(file);
        return NULL;
    }
    bytes = malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        free(bytes); fclose(file); return NULL;
    }
    fclose(file); *size_out = (size_t)length; return bytes;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "../../build/cli-rt/exec-matrix.wasm";
    size_t size = 0;
    uint8_t *bytes = read_all(path, &size);
    native_store store;
    native_exec_request request;
    native_process_image *image = NULL;
    exec_error error;
    int status = 0;
    int child = 0;
    CHECK(bytes != NULL, "read valid executable fixture");
    if (!bytes) return 1;
    native_store_init(&store);
    CHECK(native_store_register_executable(&store, "/bin/matrix", bytes, size,
                                           0755u, 1u, "_start") == 0,
          "register valid executable");
    {
        posix_path_metadata metadata;
        CHECK(posix_kernel_path_stat(store.kernel,
                                     (const uint8_t *)"/bin/matrix", 11, 1,
                                     &metadata) == 0 &&
              metadata.kind == POSIX_NODE_REGULAR && metadata.mode == 0755u &&
              metadata.size == (int64_t)size,
              "registered executable is visible through kernel VFS");
        CHECK(posix_kernel_path_access(store.kernel,
                                       (const uint8_t *)"/bin/matrix", 11,
                                       POSIX_X_OK, 0) == 0,
              "registered executable has executable VFS permission");
        native_store_enable_terminal(&store);
        CHECK(posix_kernel_path_access(store.kernel,
                                       (const uint8_t *)"/bin/matrix", 11,
                                       POSIX_X_OK, 0) == 0,
              "executable binding survives terminal kernel replacement");
        CHECK(posix_kernel_path_stat(store.kernel,
                                     (const uint8_t *)"/bin/missing", 12, 1,
                                     &metadata) == -POSIX_ENOENT,
              "missing path metadata returns ENOENT");
        CHECK(posix_kernel_path_access(store.kernel,
                                       (const uint8_t *)"/bin/missing", 12,
                                       POSIX_X_OK, 0) == -POSIX_ENOENT,
              "missing path access returns ENOENT");
        {
            const posix_path_metadata nonexec = {
                POSIX_NODE_REGULAR, 0644u, 0, 0, 17, 40
            };
            const posix_path_metadata symlink = {
                POSIX_NODE_SYMLINK, 0777u, 0, 0, 11, 41
            };
            CHECK(posix_kernel_path_add(store.kernel, "/tmp/noexec",
                                        &nonexec) == 0,
                  "add non-executable regular-file fixture");
            CHECK(posix_kernel_path_access(store.kernel,
                                           (const uint8_t *)"/tmp/noexec", 11,
                                           POSIX_X_OK, 0) == -POSIX_EACCES,
                  "non-executable file access returns EACCES");
            CHECK(posix_kernel_path_add(store.kernel, "/tmp/probe-link",
                                        &symlink) == 0,
                  "add symlink metadata fixture");
            CHECK(posix_kernel_path_stat(store.kernel,
                                         (const uint8_t *)"/tmp/probe-link",
                                         15, 0, &metadata) == 0 &&
                  metadata.kind == POSIX_NODE_SYMLINK,
                  "symlink metadata remains distinguishable");
        }
    }
    CHECK(native_store_register_executable(&store, "/bin/matrix", bytes, size,
                                           0755u, 1u, "_start") == -POSIX_EEXIST,
          "duplicate registration rejected");
    CHECK(native_store_register_executable(&store, "/bin/noexec", bytes, size,
                                           0644u, 1u, "_start") == -POSIX_EINVAL,
          "permission failure rejected");
    CHECK(native_store_register_executable(&store, "/bin/badabi", bytes, size,
                                           0755u, 2u, "_start") == -POSIX_EINVAL,
          "ABI failure rejected");
    CHECK(native_store_register_executable(&store, "/bin/badentry", bytes, size,
                                           0755u, 1u, "main") == -POSIX_EINVAL,
          "entry failure rejected");
    CHECK(native_store_register_executable(&store, "/bin/badwasm",
                                           (const uint8_t *)"bad", 3,
                                           0755u, 1u, "_start") == -POSIX_ENOEXEC,
          "malformed image rejected");

    memset(&request, 0, sizeof(request));
    request.active = 1;
    request.pid = 1;
    snprintf(request.path, sizeof(request.path), "/bin/missing");
    memset(&error, 0, sizeof(error));
    CHECK(native_store_instantiate_executable(&store, &request, &image,
                                              &error) == EXEC_ERROR_NOT_FOUND,
          "missing executable fails before commit");
    request.path[0] = '\0';
    CHECK(native_store_prepare_process_exec(&store, &request) == 0,
          "prepare transition succeeds for owned request");
    native_store_abort_process_exec(&store);
    request.path[0] = '\0';
    request.active = 0;
    CHECK(native_store_instantiate_executable(&store, &request, &image,
                                              &error) == EXEC_ERROR_FORMAT,
          "inactive request rejected");

    request.active = 1;
    request.pid = 1;
    snprintf(request.path, sizeof(request.path), "/bin");
    memset(&error, 0, sizeof(error));
    CHECK(native_store_instantiate_executable(&store, &request, &image,
                                              &error) == EXEC_ERROR_NOT_FOUND,
          "directory cannot be instantiated as an executable");
    snprintf(request.path, sizeof(request.path), "/tmp/probe-link");
    memset(&error, 0, sizeof(error));
    CHECK(native_store_instantiate_executable(&store, &request, &image,
                                              &error) == EXEC_ERROR_NOT_FOUND,
          "symlink without an executable manifest is not runnable");

    memset(&request, 0, sizeof(request));
    request.active = 1; request.pid = 1;
    snprintf(request.path, sizeof(request.path), "/bin/matrix");
    memset(&error, 0, sizeof(error));
    CHECK(native_store_instantiate_executable(&store, &request, &image,
                                              &error) == EXEC_OK && image,
          "valid candidate instantiates before commit");
    if (image) native_process_image_release(image);

    CHECK(native_store_fork_process(&store, &child) == 0,
          "fork for termination matrix");
    CHECK(native_store_set_active_process(&store, child) == 0,
          "select child for trap termination");
    CHECK(native_store_exit_process(&store, 127) == 0,
          "trap termination records status");
    CHECK(native_store_set_active_process(&store, 1) == 0,
          "restore parent after trap");
    CHECK(native_store_wait_process(&store, child, 0, &status) == child &&
          status == (127 << 8), "trap status is reapable");

    CHECK(native_store_fork_process(&store, &child) == 0,
          "fork for signal termination");
    CHECK(native_store_set_active_process(&store, child) == 0,
          "select child for signal termination");
    CHECK(native_store_exit_process(&store, 130) == 0,
          "signal termination records shell status");
    CHECK(native_store_set_active_process(&store, 1) == 0,
          "restore parent after signal");
    CHECK(native_store_wait_process(&store, child, 0, &status) == child &&
          status == (130 << 8), "signal status is reapable");

    free(bytes);
    native_store_free(&store);
    printf("C-engine exec transition matrix: %d checks, %d failures\n",
           checks, failures);
    return failures ? 1 : 0;
}
