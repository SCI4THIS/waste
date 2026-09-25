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
#define NATIVE_EXEC_INTERPRETER_MAX 4u

typedef enum {
    NATIVE_EXEC_HANDLER_NONE = 0,
    NATIVE_EXEC_HANDLER_WAST = 1
} native_exec_handler_kind;

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
    uint8_t interpreter_depth;
    uint8_t handler_kind;
    uint8_t *handler_bytes;
    size_t handler_size;
} native_exec_request;

struct native_process_image {
    waste_exec_engine *engine;
    uint32_t entry_func;
    char path[NATIVE_EXEC_PATH_MAX];
    int pid;
    char cwd[POSIX_PATH_NODE_NAME_MAX];
    char *argv[NATIVE_EXEC_ARG_MAX];
    uint32_t argc;
    char *envp[NATIVE_EXEC_ENV_MAX];
    uint32_t envc;
    uint32_t startup_ptr;
    uint32_t startup_size;
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

typedef enum {
    NATIVE_PROCESS_HANDLER_NONE = 0,
    NATIVE_PROCESS_HANDLER_WAST = 1
} native_process_handler_kind;

typedef void (*native_process_handler_context_destroy)(void *context);
typedef exec_status (*native_process_handler_step)(
    const uint8_t *source, size_t source_size, size_t offset, unsigned line,
    size_t *next_offset, unsigned *next_line, void *context);

/* Process-owned non-Wasm execution state. The source buffer and cursor are
 * deliberately separate from native_process_image: a WAST handler may load
 * several modules and resume between commands. */
typedef struct {
    native_process_handler_kind kind;
    uint8_t *source;
    size_t source_size;
    size_t stream_offset;
    unsigned stream_line;
    void *context;
    native_process_handler_context_destroy destroy_context;
    exec_status status;
    int exit_code;
    exec_yield_reason wait_reason;
} native_process_handler;

typedef struct native_store_checkpoint native_store_checkpoint;

typedef struct {
    uint64_t address;
    uint64_t length;
    uint64_t file_offset;
    posix_file_object *file_object;
    uint8_t shared;
    uint8_t writable;
} native_process_file_mapping;

typedef struct {
    posix_file_object *file_object;
    uint64_t file_offset;
    exec_memory_page *page;
} native_shared_file_page;

typedef enum {
    NATIVE_PROCESS_REGION_MODULE = 1,
    NATIVE_PROCESS_REGION_STACK,
    NATIVE_PROCESS_REGION_STARTUP,
    NATIVE_PROCESS_REGION_BRK,
    NATIVE_PROCESS_REGION_MAPPING,
    NATIVE_PROCESS_REGION_GUARD
} native_process_region_kind;

typedef struct {
    uint64_t first_page;
    uint64_t page_count;
    native_process_region_kind kind;
} native_process_region;

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
    native_process_file_mapping *file_mappings;
    uint32_t file_mapping_count;
    uint32_t file_mapping_capacity;
    native_process_region *regions;
    uint32_t region_count;
    uint32_t region_capacity;
    uint64_t virtual_page_limit;
    native_process_handler handler;
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
    posix_shm_namespace *shm_namespace;
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
    native_shared_file_page *shared_file_pages;
    uint32_t shared_file_page_count;
    uint32_t shared_file_page_capacity;
} native_store;

void native_exec_request_init(native_exec_request *request);
void native_exec_request_destroy(native_exec_request *request);
int native_exec_request_take_handler(native_exec_request *request,
                                     native_exec_handler_kind kind,
                                     uint8_t **bytes_out, size_t *size_out);
void native_process_image_init(native_process_image *image);
void native_process_image_retain(native_process_image *image);
void native_process_image_pin(native_process_image *image);
void native_process_image_unpin(native_process_image *image);
void native_process_image_release(native_process_image *image);
void native_process_capsule_init(native_process_capsule *capsule);
int native_process_capsule_clone(native_process_capsule *destination,
                                 const native_process_capsule *source);
void native_process_capsule_destroy(native_process_capsule *capsule);
int native_process_capsule_select_entry(native_process_capsule *capsule,
                                         waste_exec_engine *engine,
                                         uint32_t func_idx,
                                         const wasm_value *args,
                                         int arg_count);
int native_process_capsule_bind_memory(native_process_capsule *capsule);
/* Process-owned region metadata is page-based and independent of Wasm
 * linear-memory visibility.  It is the address-space allocator's collision
 * boundary; mappings remain represented by exec_memory VMAs. */
int native_process_capsule_reserve_region(
    native_process_capsule *capsule, uint64_t first_page,
    uint64_t page_count, native_process_region_kind kind);
int native_process_capsule_region_is_reserved(
    const native_process_capsule *capsule, uint64_t first_page,
    uint64_t page_count, native_process_region_kind *kind_out);
int native_process_capsule_allocate_region(
    native_process_capsule *capsule, uint64_t page_count,
    native_process_region_kind kind, int top_down, uint64_t *first_page_out);
void native_process_capsule_clear_regions(native_process_capsule *capsule);
/* Process-owned virtual-memory entry points for the future POSIX mmap
 * adapter. Page numbers are engine virtual pages, not host pointers. */
int native_process_capsule_map_pages(native_process_capsule *capsule,
                                     uint64_t first_page, uint64_t page_count,
                                     uint8_t protection, uint8_t flags);
int native_process_capsule_unmap_pages(native_process_capsule *capsule,
                                       uint64_t first_page,
                                       uint64_t page_count);
int native_process_capsule_mmap_range(native_process_capsule *capsule,
                                      uint64_t address, uint64_t length,
                                      uint8_t protection, uint8_t flags,
                                      uint64_t *address_out);
int native_process_capsule_munmap_range(native_process_capsule *capsule,
                                        uint64_t address, uint64_t length);
int native_process_capsule_mprotect_range(native_process_capsule *capsule,
                                          uint64_t address, uint64_t length,
                                          uint8_t protection);
int native_process_capsule_record_file_mapping(
    native_process_capsule *capsule, uint64_t address, uint64_t length,
    posix_file_object *file_object, uint64_t file_offset, int shared,
    int writable);
int native_process_capsule_forget_file_mapping(
    native_process_capsule *capsule, uint64_t address, uint64_t length);
int native_process_capsule_install_handler(
    native_process_capsule *capsule, native_process_handler_kind kind,
    uint8_t *source, size_t source_size, void *context,
    native_process_handler_context_destroy destroy_context);
int native_process_capsule_attach_handler_context(
    native_process_capsule *capsule, void *context,
    native_process_handler_context_destroy destroy_context);
/* Borrow the handler source and current cursor for one command-driver step. */
int native_process_capsule_handler_cursor(
    const native_process_capsule *capsule, const uint8_t **source_out,
    size_t *size_out, size_t *offset_out, unsigned *line_out);
/* Commit a monotonically advancing source cursor after a resumable command. */
int native_process_capsule_advance_handler(native_process_capsule *capsule,
                                           size_t offset, unsigned line);
/* Mark a handler blocked at an ordinary host wait and resume it later. */
int native_process_capsule_suspend_handler(native_process_capsule *capsule,
                                           native_process_run_state state);
int native_process_capsule_resume_handler(native_process_capsule *capsule);
int native_process_capsule_set_handler_exit_code(native_process_capsule *capsule,
                                                 int exit_code);
/* Run one parser/command-driver step. A yielded step may leave the cursor at
 * the same command; failed steps never commit their proposed cursor. */
exec_status native_process_capsule_run_handler_step(
    native_process_capsule *capsule, native_process_handler_step callback,
    void *context);
/* Run a handler step with the context owned by the capsule.  Browser drivers
 * use this form so a later resume cannot accidentally depend on stack state. */
exec_status native_process_capsule_run_attached_handler_step(
    native_process_capsule *capsule, native_process_handler_step callback);
/* Dispatch one step through the store's currently selected process. */
exec_status native_store_run_process_handler_step(
    native_store *store, native_process_handler_step callback);
int native_store_process_handler_cursor(
    const native_store *store, const uint8_t **source_out,
    size_t *size_out, size_t *offset_out, unsigned *line_out);
int native_store_process_handler_context(const native_store *store,
                                         void **context_out);
int native_store_process_handler_result(const native_store *store,
                                        exec_status *status_out,
                                        int *exit_code_out);
int native_store_advance_process_handler(native_store *store,
                                          size_t offset, unsigned line);
int native_store_suspend_process_handler(native_store *store,
                                         native_process_run_state state);
int native_store_suspend_process_handler_for_yield(
    native_store *store, exec_yield_reason reason);
int native_store_process_handler_wait_reason(
    const native_store *store, exec_yield_reason *reason_out);
int native_store_resume_process_handler(native_store *store);
void native_process_capsule_clear_handler(native_process_capsule *capsule);
/* Finish the active handler and enter the ordinary process-exit path. The
 * handler payload/context are released before the process becomes a zombie. */
int native_store_complete_process_handler(native_store *store,
                                           exec_status status, int exit_code);
/* Complete a handler using the shell-visible default status for its result. */
int native_store_complete_process_handler_default(native_store *store,
                                                  exec_status status);
int native_store_complete_process_handler_signal(native_store *store,
                                                 exec_status status,
                                                 int signal);
/* Browser/process-driver handoff: validate the live parent and its transition
 * slot, publish child completion, then queue exactly one parent wake. */
int native_store_complete_process_handler_and_wake(
        native_store *store, exec_status status, int exit_code);
/* Complete and wake using the handler result recorded by its last step. */
int native_store_complete_process_handler_result_and_wake(native_store *store);
int native_store_complete_process_handler_and_wake_default(
        native_store *store, exec_status status);
int native_process_handler_default_exit_code(exec_status status,
                                             int explicit_exit_code);
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
/* Install the immutable interpreter handler nodes used by loader-owned
 * shebang dispatch. Their contents are intentionally empty until the handler
 * image implementation is installed. */
int native_store_bind_interpreter_paths(native_store *store);
const native_executable *native_store_find_executable(
    const native_store *store, const char *path);
exec_status native_store_instantiate_executable(
    native_store *store, native_exec_request *request,
    native_process_image **image_out, exec_error *error);
int native_store_commit_process_image(native_store *store,
                                      native_process_image *image);
int native_store_commit_process_handler(native_store *store,
                                        native_exec_request *request,
                                        native_process_handler_kind kind);
int native_store_commit_process_handler_with_context(
    native_store *store, native_exec_request *request,
    native_process_handler_kind kind, void *context,
    native_process_handler_context_destroy destroy_context);
int native_store_prepare_process_exec(native_store *store,
                                      const native_exec_request *request);
void native_store_abort_process_exec(native_store *store);
int native_store_wake_process(native_store *store, int pid, int result);
void native_store_complete_process_wake(native_store *store);
int native_store_take_process_wake(native_store *store, int *result_out);

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
int native_store_shared_file_page(native_store *store,
                                  posix_file_object *file_object,
                                  uint64_t file_offset,
                                  exec_memory_page **page_out);

int native_store_getpid(const native_store *store);
int native_store_getppid(const native_store *store);
int native_store_set_active_process(native_store *store, int pid);
int native_store_fork_process(native_store *store, int *pid_out);
int native_store_clone_process_graph(native_store *store, int parent_pid,
                                     int child_pid);
int native_store_exit_process(native_store *store, int status);
int native_store_signal_process(native_store *store, int pid, int signal);
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
