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

int wast_parse_bytes(const char *bytes, size_t length, wast_script *script) {
    (void)bytes; (void)length;
    if (script) memset(script, 0, sizeof(*script));
    return -1;
}

void wast_script_free(wast_script *script) {
    (void)script;
}

uint8_t *wast_encode_module(const wast_module *module, size_t *size,
                            char *error) {
    (void)module; (void)size; (void)error;
    return NULL;
}

static int checks;
static int failures;
static int rejected_context_destroyed;

static void destroy_rejected_context(void *context) {
    rejected_context_destroyed++;
    free(context);
}
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
                POSIX_NODE_REGULAR, 0644u, 0, 0, 17, 40, 0, 0
            };
            const posix_path_metadata symlink = {
                POSIX_NODE_SYMLINK, 0777u, 0, 0, 11, 41, 0, 0
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
    request.argv[0] = (char *)malloc(8);
    request.argv[1] = (char *)malloc(6);
    request.envp[0] = (char *)malloc(9);
    if (request.argv[0]) memcpy(request.argv[0], "matrix", 7);
    if (request.argv[1]) memcpy(request.argv[1], "first", 6);
    if (request.envp[0]) memcpy(request.envp[0], "K=VALUE", 8);
    request.argc = 2;
    request.envc = 1;
    memset(&error, 0, sizeof(error));
    CHECK(native_store_instantiate_executable(&store, &request, &image,
                                              &error) == EXEC_OK && image,
          "valid candidate instantiates before commit");
    CHECK(image && image->pid == 1 && strcmp(image->cwd, "/") == 0,
          "startup pid and cwd copied");
    CHECK(image && image->argc == 2 && strcmp(image->argv[1], "first") == 0,
          "startup argv copied");
    CHECK(image && image->envc == 1 && strcmp(image->envp[0], "K=VALUE") == 0,
          "startup environment copied");
    CHECK(image && image->startup_ptr != 0 && image->startup_size > 44,
          "startup block materialized");
    if (image && image->engine && image->engine->memory) {
        uint8_t startup_byte = 0;
        uint8_t cwd_pointer_bytes[4] = {0};
        uint8_t cwd_bytes[2] = {0};
        uint32_t cwd_pointer;
        CHECK(exec_memory_read(image->engine->memory, 100, &startup_byte, 1,
                               &error) == EXEC_OK &&
                  startup_byte == (uint8_t)image->startup_ptr,
              "startup hook received block pointer");
        CHECK(exec_memory_read(image->engine->memory, image->startup_ptr + 20u,
                               cwd_pointer_bytes, sizeof(cwd_pointer_bytes),
                               &error) == EXEC_OK,
              "startup cwd pointer is readable");
        cwd_pointer = (uint32_t)cwd_pointer_bytes[0] |
            ((uint32_t)cwd_pointer_bytes[1] << 8) |
            ((uint32_t)cwd_pointer_bytes[2] << 16) |
            ((uint32_t)cwd_pointer_bytes[3] << 24);
        CHECK(exec_memory_read(image->engine->memory, cwd_pointer, cwd_bytes,
                               sizeof(cwd_bytes), &error) == EXEC_OK &&
                  memcmp(cwd_bytes, "/", sizeof(cwd_bytes)) == 0,
              "startup cwd pointer follows argv and envp strings");
    }
    if (image) native_process_image_release(image);

    {
        native_process_image *pinned_image = calloc(1, sizeof(*pinned_image));
        CHECK(pinned_image != NULL, "continuation image pin allocates");
        if (pinned_image) {
            pinned_image->references = 2;
            native_process_image_pin(pinned_image);
            native_process_image_release(pinned_image);
            CHECK(pinned_image->references == 1 &&
                  pinned_image->checkpoint_pins == 1,
                  "continuation pin survives one image release");
            native_process_image_release(pinned_image);
            CHECK(pinned_image->references == 0 &&
                  pinned_image->checkpoint_pins == 1,
                  "continuation pin keeps released image alive");
            native_process_image_unpin(pinned_image);
        }
    }

    {
        waste_exec_engine continuation_engine;
        exec_continuation continuation;
        exec_error continuation_error;
        memset(&continuation_engine, 0, sizeof(continuation_engine));
        continuation_engine.func_count = 7;
        continuation_engine.import_func_count = 3;
        exec_continuation_init(&continuation);
        memset(&continuation_error, 0, sizeof(continuation_error));
        CHECK(exec_continuation_capture(&continuation_engine, &continuation,
                                        &continuation_error) == EXEC_OK &&
              exec_continuation_resume(&continuation, &continuation_engine,
                                       0, EXEC_YIELD_NONE,
                                       &continuation_error) == EXEC_OK &&
              continuation_engine.func_count == 7 &&
              continuation_engine.import_func_count == 3,
              "continuation restore preserves engine function tables");
        exec_continuation_destroy(&continuation);
    }

    {
        waste_exec_engine fake_engine;
        native_process_image invalid_image;
        memset(&fake_engine, 0, sizeof(fake_engine));
        fake_engine.func_count = 1;
        memset(&invalid_image, 0, sizeof(invalid_image));
        invalid_image.engine = &fake_engine;
        invalid_image.entry_func = 1;
        invalid_image.references = 1;
        CHECK(native_store_commit_process_image(&store, &invalid_image) ==
                  -POSIX_EINVAL &&
              native_store_active_capsule(&store)->image == NULL,
              "image commit rejects an out-of-range entry atomically");
        invalid_image.references = 0;

        native_process_image direct_image;
        memset(&direct_image, 0, sizeof(direct_image));
        direct_image.engine = &fake_engine;
        direct_image.references = 1;
        CHECK(native_store_commit_process_image(&store, &direct_image) ==
                  -POSIX_EINVAL &&
              native_store_active_capsule(&store)->image == NULL,
              "image commit requires an explicit exec transition");
        direct_image.references = 0;
    }

    {
        native_exec_request handler_request;
        uint8_t *handler_bytes = NULL;
        size_t handler_size = 0;
        native_exec_request_init(&handler_request);
        handler_request.handler_kind = NATIVE_EXEC_HANDLER_WAST;
        handler_request.handler_size = 3;
        handler_request.handler_bytes = (uint8_t *)malloc(3);
        if (handler_request.handler_bytes)
            memcpy(handler_request.handler_bytes, "wat", 3);
        CHECK(native_exec_request_take_handler(
                  &handler_request, NATIVE_EXEC_HANDLER_WAST,
                  &handler_bytes, &handler_size) == 0 &&
              handler_bytes && handler_size == 3 &&
              memcmp(handler_bytes, "wat", 3) == 0 &&
              handler_request.handler_kind == NATIVE_EXEC_HANDLER_NONE &&
              handler_request.handler_bytes == NULL &&
              handler_request.handler_size == 0,
              "WAST handler payload transfers ownership exactly once");
        free(handler_bytes);
        CHECK(native_exec_request_take_handler(
                  &handler_request, NATIVE_EXEC_HANDLER_WAST,
                  &handler_bytes, &handler_size) == -POSIX_EINVAL,
              "consumed WAST handler cannot be taken twice");
        native_exec_request_destroy(&handler_request);
        native_exec_request oversized_request;
        native_exec_request_init(&oversized_request);
        oversized_request.handler_kind = NATIVE_EXEC_HANDLER_WAST;
        oversized_request.handler_size = NATIVE_EXEC_BYTES_MAX + 1u;
        oversized_request.handler_bytes = (uint8_t *)malloc(1);
        CHECK(native_exec_request_take_handler(
                  &oversized_request, NATIVE_EXEC_HANDLER_WAST,
                  &handler_bytes, &handler_size) == -POSIX_EINVAL &&
              oversized_request.handler_bytes != NULL,
              "oversized WAST handler transfer preserves ownership");
        native_exec_request_destroy(&oversized_request);
    }

    {
        native_exec_request handler_request;
        native_exec_request invalid_request;
        native_process_capsule *capsule;
        native_process_image *prior_image = (native_process_image *)calloc(
            1, sizeof(*prior_image));
        if (prior_image) prior_image->references = 1;
        native_store_active_capsule(&store)->image = prior_image;
        native_exec_request_init(&invalid_request);
        invalid_request.active = 1;
        invalid_request.pid = 1;
        invalid_request.handler_kind = NATIVE_EXEC_HANDLER_WAST;
        CHECK(native_store_prepare_process_exec(&store, &invalid_request) == 0 &&
              native_store_commit_process_handler(
                  &store, &invalid_request, NATIVE_PROCESS_HANDLER_WAST) ==
                  -POSIX_EINVAL &&
              invalid_request.handler_kind == NATIVE_EXEC_HANDLER_WAST &&
              invalid_request.handler_bytes == NULL &&
              native_store_active_capsule(&store)->pending_transition ==
                  NATIVE_PROCESS_TRANSITION_EXEC,
              "missing WAST payload is rejected without consuming request");
        native_store_abort_process_exec(&store);
        native_exec_request_destroy(&invalid_request);

        {
            native_exec_request oversized_request;
            native_exec_request_init(&oversized_request);
            oversized_request.active = 1;
            oversized_request.pid = 1;
            oversized_request.handler_kind = NATIVE_EXEC_HANDLER_WAST;
            oversized_request.handler_size = NATIVE_EXEC_BYTES_MAX + 1u;
            oversized_request.handler_bytes = (uint8_t *)malloc(1);
            CHECK(native_store_prepare_process_exec(&store,
                                                     &oversized_request) == 0 &&
                  native_store_commit_process_handler(
                      &store, &oversized_request,
                      NATIVE_PROCESS_HANDLER_WAST) == -POSIX_EINVAL &&
                  oversized_request.handler_bytes != NULL &&
                  oversized_request.handler_size == NATIVE_EXEC_BYTES_MAX + 1u,
                  "oversized WAST handler is rejected without consuming request");
            native_store_abort_process_exec(&store);
            native_exec_request_destroy(&oversized_request);
        }

        {
            native_exec_request queued_request;
            native_exec_request_init(&queued_request);
            queued_request.active = 1;
            queued_request.pid = 1;
            queued_request.handler_kind = NATIVE_EXEC_HANDLER_WAST;
            queued_request.handler_size = 3;
            queued_request.handler_bytes = (uint8_t *)malloc(3);
            if (queued_request.handler_bytes)
                memcpy(queued_request.handler_bytes, "wst", 3);
            native_store_active_capsule(&store)->pending_result = 42;
            native_store_active_capsule(&store)->pending_result_valid = 1;
            CHECK(native_store_prepare_process_exec(&store, &queued_request) ==
                      -POSIX_EBUSY &&
                  queued_request.handler_bytes != NULL &&
                  native_store_active_capsule(&store)->pending_result == 42 &&
                  native_store_active_capsule(&store)->image == prior_image,
                  "queued wake blocks handler preparation atomically");
            native_store_active_capsule(&store)->pending_result_valid = 0;
            native_exec_request_destroy(&queued_request);
        }

        native_exec_request_init(&handler_request);
        int handler_context_marker = 7;
        handler_request.active = 1;
        handler_request.pid = 1;
        handler_request.handler_kind = NATIVE_EXEC_HANDLER_WAST;
        handler_request.handler_size = 6;
        handler_request.handler_bytes = (uint8_t *)malloc(6);
        if (handler_request.handler_bytes)
            memcpy(handler_request.handler_bytes, "(wast)", 6);
        CHECK(native_store_prepare_process_exec(&store, &handler_request) == 0 &&
              native_store_commit_process_handler_with_context(
                  &store, &handler_request, NATIVE_PROCESS_HANDLER_WAST,
                  &handler_context_marker, NULL) == 0,
              "WAST exec payload commits into active handler state");
        capsule = native_store_active_capsule(&store);
        CHECK(capsule && capsule->image == NULL &&
              capsule->handler.kind == NATIVE_PROCESS_HANDLER_WAST &&
              capsule->handler.source_size == 6 &&
              memcmp(capsule->handler.source, "(wast)", 6) == 0 &&
              capsule->handler.context == &handler_context_marker &&
              capsule->pending_transition == NATIVE_PROCESS_TRANSITION_NONE,
              "handler commit replaces image and context state atomically");
        native_exec_request_destroy(&handler_request);

        {
            waste_exec_engine fake_engine;
            native_process_image blocked_image;
            memset(&fake_engine, 0, sizeof(fake_engine));
            fake_engine.func_count = 1;
            memset(&blocked_image, 0, sizeof(blocked_image));
            blocked_image.engine = &fake_engine;
            blocked_image.references = 1;
            CHECK(native_store_commit_process_image(&store, &blocked_image) ==
                      -POSIX_EINVAL &&
                  capsule->handler.kind == NATIVE_PROCESS_HANDLER_WAST &&
                  capsule->handler.source_size == 6,
                  "image commit cannot replace an active handler");
            blocked_image.references = 0;
        }

        {
            native_exec_request duplicate_request;
            uint8_t *duplicate_context = (uint8_t *)malloc(1);
            native_exec_request_init(&duplicate_request);
            duplicate_request.active = 1;
            duplicate_request.pid = 1;
            duplicate_request.handler_kind = NATIVE_EXEC_HANDLER_WAST;
            duplicate_request.handler_size = 3;
            duplicate_request.handler_bytes = (uint8_t *)malloc(3);
            if (duplicate_request.handler_bytes)
                memcpy(duplicate_request.handler_bytes, "new", 3);
            CHECK(native_store_prepare_process_exec(
                      &store, &duplicate_request) == 0 &&
                  native_store_commit_process_handler_with_context(
                      &store, &duplicate_request,
                      NATIVE_PROCESS_HANDLER_WAST, duplicate_context,
                      destroy_rejected_context) == -POSIX_EINVAL &&
                  duplicate_request.handler_kind == NATIVE_EXEC_HANDLER_WAST &&
                  duplicate_request.handler_bytes != NULL &&
                  duplicate_request.handler_size == 3 &&
                  memcmp(capsule->handler.source, "(wast)", 6) == 0 &&
                  capsule->handler.context == &handler_context_marker &&
                  rejected_context_destroyed == 0,
                  "duplicate handler commit preserves request, context, and active handler");
            native_store_abort_process_exec(&store);
            native_exec_request_destroy(&duplicate_request);
            destroy_rejected_context(duplicate_context);
        }

        CHECK(native_store_complete_process_handler(&store, EXEC_OK, 0) ==
                  -POSIX_EINVAL &&
              capsule->handler.kind == NATIVE_PROCESS_HANDLER_WAST,
              "handler completion rejects an init process without consuming state");
        native_process_capsule_clear_handler(capsule);
    }

    {
        waste_exec_engine parent_engine;
        native_process_capsule *parent_capsule =
            native_store_active_capsule(&store);
        native_process_capsule *child_capsule;
        waste_exec_engine *child_engine;
        int graph_child = 0;
        memset(&parent_engine, 0, sizeof(parent_engine));
        parent_engine.func_count = 7;
        parent_engine.import_func_count = 3;
        parent_capsule->engine = &parent_engine;
        parent_capsule->root_func_idx = 0;
        CHECK(native_store_fork_process(&store, &graph_child) == 0 &&
              native_store_set_active_process(&store, graph_child) == 0,
              "fork creates an executable process graph child");
        child_capsule = native_store_active_capsule(&store);
        child_engine = child_capsule ? child_capsule->engine : NULL;
        CHECK(child_engine && child_engine != &parent_engine &&
              child_engine->func_count == 7 &&
              child_engine->import_func_count == 3,
              "fork child starts with an independent engine table");
        CHECK(native_store_clone_process_graph(&store, 1, graph_child) == 0 &&
              parent_engine.func_count == 7 &&
              parent_engine.import_func_count == 3 && child_engine &&
              child_engine->func_count == 7 &&
              child_engine->import_func_count == 3,
              "process graph binding preserves parent and child tables");
        parent_capsule->engine = NULL;
        if (child_capsule) child_capsule->engine = NULL;
        store.active_pid = 1;
        CHECK(native_store_set_active_process(&store, graph_child) == 0 &&
              native_store_exit_process(&store, 0) == 0 &&
              native_store_set_active_process(&store, 1) == 0 &&
              native_store_wait_process(&store, graph_child, 0, &status) == graph_child,
              "reaping releases the owned fork root without a harness-side free");
    }

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

    native_exec_request_destroy(&request);
    free(bytes);
    native_store_free(&store);
    printf("C-engine exec transition matrix: %d checks, %d failures\n",
           checks, failures);
    return failures ? 1 : 0;
}
