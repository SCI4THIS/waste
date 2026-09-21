#ifndef WASTE_RUNTIME_STORE_H
#define WASTE_RUNTIME_STORE_H

#include "runtime_internal.h"
#include "wat/types.h"
#include "lib/include/kernel.h"
#include <stddef.h>

typedef struct {
    waste_exec_engine *engine;
    uint32_t func_idx;
} native_linked_func;

typedef struct native_call_block {
    native_linked_func *calls;
    struct native_call_block *next;
} native_call_block;

typedef struct {
    waste_exec_engine *engine;
    const wast_module *module;
    char id[WAST_MAX_EXPORT_NAME];
    char registered[WAST_MAX_EXPORT_NAME];
} native_linked_module;

#define NATIVE_PROCESS_MAX 16
#define NATIVE_EXEC_PATH_MAX 256
#define NATIVE_EXEC_ARG_MAX 128
#define NATIVE_EXEC_ENV_MAX 256
#define NATIVE_EXEC_BYTES_MAX (16u * 1024u * 1024u)

typedef struct native_process_image native_process_image;

/* Immutable executable metadata. Bytes are owned by the store registry and
 * remain valid while any process image is instantiated from this entry. */
typedef struct {
    char path[NATIVE_EXEC_PATH_MAX];
    uint8_t *bytes;
    size_t size;
    uint32_t abi_version;
    uint32_t mode;
    char entry[WAST_MAX_EXPORT_NAME];
    uint8_t validated;
} native_executable;

/* Owned copy of an execve request. No guest pointer may cross the import
 * boundary or survive a yield. */
typedef struct {
    char path[NATIVE_EXEC_PATH_MAX];
    char *argv[NATIVE_EXEC_ARG_MAX];
    uint32_t argc;
    char *envp[NATIVE_EXEC_ENV_MAX];
    uint32_t envc;
    int pid;
    int failure_errno;
    uint8_t active;
} native_exec_request;

struct native_process_image {
    waste_exec_engine *engine;
    uint32_t entry_func;
    char path[NATIVE_EXEC_PATH_MAX];
    uint32_t references;
    uint32_t checkpoint_pins;
};

typedef enum {
    NATIVE_PROCESS_RUNNABLE = 0,
    NATIVE_PROCESS_BROWSER_BLOCKED,
    NATIVE_PROCESS_WAIT_BLOCKED,
    NATIVE_PROCESS_EXITED
} native_process_run_state;

typedef enum {
    NATIVE_PROCESS_TRANSITION_NONE = 0,
    NATIVE_PROCESS_TRANSITION_EXEC,
    NATIVE_PROCESS_TRANSITION_EXIT,
    NATIVE_PROCESS_TRANSITION_WAKE
} native_process_transition;

typedef struct native_store_checkpoint native_store_checkpoint;

/* Process-owned execution identity.  The browser driver may cache a pointer
 * to this capsule, but it is the store—not browser globals—that owns the
 * image, evaluator descriptor, and suspended continuation. */
typedef struct {
    waste_exec_engine *engine;
    native_process_image *image;
    uint32_t root_func_idx;
    wasm_value root_args[WAST_MAX_ARGS];
    int root_arg_count;
    exec_continuation *continuation;
    uint64_t generation;
    native_process_run_state state;
    native_process_transition pending_transition;
    int pending_result;
    int pending_error;
    uint8_t pending_result_valid;
    uint8_t continuation_valid;
    native_store_checkpoint *checkpoint;
    waste_exec_engine **linked_engines;
    uint32_t linked_engine_count;
    waste_exec_engine **continuation_engines;
    exec_continuation *continuations;
    uint32_t continuation_count;
} native_process_capsule;

typedef struct {
    int used;
    int pid;
    int ppid;
    int zombie;
    int exit_status;
    posix_kernel *kernel;
    native_process_capsule capsule;
} native_process;

/* Host function binding returned by a resolver callback. */
typedef struct {
    exec_host_func function;
    void *host_data;
    exec_host_control control;
} native_host_binding;

/* Optional callback for resolving host-provided function imports that no
   registered Wasm module provides (e.g. POSIX stubs in the browser build).
   Returns 1 if resolved, 0 otherwise. */
typedef int (*native_host_resolver)(const char *module, const char *name,
                                     void *context, native_host_binding *out);

struct posix_kernel;

typedef struct native_store {
    native_linked_module *modules;
    int module_count;
    int module_capacity;
    native_call_block *call_blocks;
    waste_exec_engine **orphan_engines;
    int orphan_count;
    int orphan_capacity;
    /* Spectest provider (for WAST script conformance tests). */
    exec_memory spectest_memory;
    exec_table spectest_table;
    exec_global spectest_i32;
    exec_global spectest_i64;
    exec_global spectest_f32;
    exec_global spectest_f64;
    /* Optional host function resolver for imports not satisfied by any
       registered Wasm module. */
    native_host_resolver host_resolver;
    void *host_context;
    /* Per-sandbox POSIX kernel: descriptor table, readiness, and wait state. */
    struct posix_kernel *kernel;
    int kernel_terminal;
    native_process processes[NATIVE_PROCESS_MAX];
    native_executable *executables;
    uint32_t executable_count;
    uint32_t executable_capacity;
    native_exec_request exec_request;
    int process_count;
    int active_pid;
    int next_pid;
    int fork_child_resume;
    int fork_parent_resume;
    int fork_parent_pid;
    int fork_child_pid;
    int last_wait_pid;
    int last_wait_status;
} native_store;

void native_exec_request_init(native_exec_request *request);
void native_exec_request_destroy(native_exec_request *request);
void native_process_image_init(native_process_image *image);
void native_process_image_retain(native_process_image *image);
void native_process_image_pin(native_process_image *image);
void native_process_image_unpin(native_process_image *image);
void native_process_image_release(native_process_image *image);
void native_process_capsule_init(native_process_capsule *capsule);
int native_process_capsule_clone(native_process_capsule *destination,
                                 const native_process_capsule *source);
void native_process_capsule_destroy(native_process_capsule *capsule);
native_process_capsule *native_store_active_capsule(native_store *store);
native_process_capsule *native_store_process_capsule(native_store *store,
                                                     int pid);

int native_store_register_executable(native_store *store, const char *path,
                                     const uint8_t *bytes, size_t size,
                                     uint32_t mode, uint32_t abi_version,
                                     const char *entry);
/* Mirror registered executable manifests into the active kernel namespace so
 * path metadata and exec lookup cannot disagree. */
int native_store_bind_executable_paths(native_store *store);
const native_executable *native_store_find_executable(
    const native_store *store, const char *path);
exec_status native_store_instantiate_executable(
    native_store *store, const native_exec_request *request,
    native_process_image **image_out, exec_error *error);
int native_store_commit_process_image(native_store *store,
                                      native_process_image *image);
int native_store_prepare_process_exec(native_store *store,
                                      const native_exec_request *request);
void native_store_abort_process_exec(native_store *store);
int native_store_wake_process(native_store *store, int pid, int result);
void native_store_complete_process_wake(native_store *store);

/* A reversible snapshot of mutable store and evaluator state. The snapshot
 * retains pointers to the live store objects, but owns copied bytes and
 * resumable frames for every linked engine; decoded modules and engine code
 * remain shared and immutable. */
struct native_store_checkpoint {
    void *impl;
};

void native_store_checkpoint_init(native_store_checkpoint *checkpoint);
exec_status native_store_checkpoint_capture(native_store *store,
                                            native_store_checkpoint *checkpoint,
                                            exec_error *error);
exec_status native_store_checkpoint_restore(native_store_checkpoint *checkpoint,
                                            exec_error *error);
void native_store_checkpoint_destroy(native_store_checkpoint *checkpoint);

void native_store_init(native_store *store);
void native_store_free(native_store *store);
void native_store_enable_terminal(native_store *store);

int native_store_getpid(const native_store *store);
int native_store_getppid(const native_store *store);
int native_store_set_active_process(native_store *store, int pid);
int native_store_fork_process(native_store *store, int *pid_out);
int native_store_clone_process_graph(native_store *store, int parent_pid,
                                     int child_pid);
int native_store_exit_process(native_store *store, int status);
int native_store_wait_process(native_store *store, int pid, int options,
                              int *status_out);

int native_store_add(native_store *store, waste_exec_engine *engine,
                     const wast_module *identity,
                     const wast_module *metadata);

int native_store_keep_orphan(native_store *store,
                              waste_exec_engine *engine);

native_linked_module *native_registered_module(native_store *store,
                                                const char *name);

waste_exec_engine *native_selected_engine(native_store *store,
                                           const char *id);

native_linked_module *native_selected_module(native_store *store,
                                              const char *id);

const wast_module *native_find_definition(const wast_script *script,
                                           int before_group,
                                           const char *id);

exec_status native_load_module(native_store *store,
                                const wast_module *module,
                                const uint8_t *bytes, size_t size,
                                waste_exec_engine **engine_out,
                                exec_error *error);

uint8_t *encode_group_module(const wast_group *group, size_t *size_out,
                              char *error);

#endif /* WASTE_RUNTIME_STORE_H */
