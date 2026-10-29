#ifndef WASTE_H
#define WASTE_H

#include <stddef.h>
#include <stdint.h>

/* ---- Value types ---- */

typedef struct { uint8_t bytes[16]; } wasm_v128;

typedef enum {
    WASM_VALTYPE_I32 = 0,
    WASM_VALTYPE_I64,
    WASM_VALTYPE_F32,
    WASM_VALTYPE_F64,
    WASM_VALTYPE_V128,
    WASM_VALTYPE_FUNCREF,
    WASM_VALTYPE_EXTERNREF,
    WASM_VALTYPE_FUNCREF_NONNULL,
    WASM_VALTYPE_EXTERNREF_NONNULL,
    WASM_VALTYPE_ANYREF,
    WASM_VALTYPE_EQREF,
    WASM_VALTYPE_I31REF,
    WASM_VALTYPE_STRUCTREF,
    WASM_VALTYPE_ARRAYREF,
    WASM_VALTYPE_ANYREF_NONNULL,
    WASM_VALTYPE_EQREF_NONNULL,
    WASM_VALTYPE_I31REF_NONNULL,
    WASM_VALTYPE_STRUCTREF_NONNULL,
    WASM_VALTYPE_ARRAYREF_NONNULL,
    WASM_VALTYPE_EXNREF,
    WASM_VALTYPE_EXNREF_NONNULL,
    WASM_VALTYPE_NULLREF,
    WASM_VALTYPE_NULLFUNCREF,
    WASM_VALTYPE_NULLEXNREF,
    WASM_VALTYPE_NULLEXTERNREF
} wasm_valtype;

#define WASM_VALTYPE_TYPE_REF_NULL_BASE 0x1000
#define WASM_VALTYPE_TYPE_REF_BASE      0x2000
#define WASM_VALTYPE_TYPE_REF_LIMIT     0x3000
#define WASM_VALTYPE_IS_TYPE_REF(t) \
    ((unsigned)(t) >= WASM_VALTYPE_TYPE_REF_NULL_BASE && \
     (unsigned)(t) < WASM_VALTYPE_TYPE_REF_LIMIT)
#define WASM_VALTYPE_TYPE_REF_INDEX(t) ((unsigned)(t) & (WASM_VALTYPE_TYPE_REF_BASE - WASM_VALTYPE_TYPE_REF_NULL_BASE - 1u))

typedef struct {
    wasm_valtype type;
    union {
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;
        wasm_v128 v128;
        uint32_t ref;
    };
    uint8_t nan_mode[16];
} wasm_value;

typedef wasm_valtype waste_value_type;
typedef wasm_v128 waste_v128;
typedef wasm_value waste_value;

/* ---- Error types ---- */

typedef enum {
    WASTE_OK = 0,
    WASTE_ERROR_MALFORMED,
    WASTE_ERROR_UNSUPPORTED,
    WASTE_ERROR_TRAP,
    WASTE_ERROR_EXCEPTION,
    WASTE_ERROR_NOT_FOUND,
    WASTE_ERROR_EXIT,
    WASTE_YIELD,
    WASTE_ERROR_INTERRUPTED,
    WASTE_ERROR_RESOURCE,
    WASTE_ERROR_LIMIT
} waste_status;

typedef struct {
    waste_status status;
    size_t offset;
    char message[256];
} waste_error;

/* ---- Engine API ---- */

typedef struct waste_module waste_module;
typedef struct waste_instance waste_instance;

waste_status waste_module_decode(const uint8_t *bytes, size_t size,
                                 waste_module **module_out,
                                 waste_error *error);
void waste_module_delete(waste_module *module);

waste_status waste_instance_create(const waste_module *module,
                                   waste_instance **instance_out,
                                   waste_error *error);
waste_status waste_instance_load(const uint8_t *bytes, size_t size,
                                 waste_instance **instance_out,
                                 waste_error *error);
void waste_instance_delete(waste_instance *instance);

waste_status waste_instance_find_function(const waste_instance *instance,
                                          const char *name,
                                          uint32_t *function_out,
                                          waste_error *error);
waste_status waste_instance_invoke(waste_instance *instance,
                                   uint32_t function,
                                   const waste_value *arguments,
                                   size_t argument_count,
                                   waste_value *results,
                                   size_t result_capacity,
                                   size_t *result_count,
                                   waste_error *error);

/* ---- Command sessions (host API 1.0) ----
 * A session owns an isolated process tree and in-memory filesystem. Input
 * strings/bytes are copied synchronously. No callback may reenter its session.
 * The native C structures below are not wire records: Wasm adapters encode
 * fixed-width fields explicitly and expose checked integer handles.
 */
#define WASTE_SESSION_ABI_MAJOR 1u
#define WASTE_SESSION_ABI_MINOR 0u
#define WASTE_SESSION_FEATURE_STREAMS 1u
#define WASTE_SESSION_FEATURE_FILES 2u
#define WASTE_SESSION_FEATURE_CANCEL 4u
#define WASTE_SESSION_FEATURE_LIMITS 8u
#define WASTE_SESSION_FEATURES 15u

typedef struct waste_session waste_session;
typedef enum {
    WASTE_SESSION_CONFIGURING = 0,
    WASTE_SESSION_RUNNABLE = 1,
    WASTE_SESSION_WAITING = 2,
    WASTE_SESSION_EXITED = 3,
    WASTE_SESSION_SIGNALED = 4,
    WASTE_SESSION_TRAPPED = 5,
    WASTE_SESSION_FAILED = 6,
    WASTE_SESSION_CANCELLED = 7,
    WASTE_SESSION_TIMED_OUT = 8,
    WASTE_SESSION_LIMIT = 9
} waste_session_state;
typedef enum {
    WASTE_SESSION_WAIT_NONE = 0,
    WASTE_SESSION_WAIT_INPUT = 1,
    WASTE_SESSION_WAIT_TIMER = 2,
    WASTE_SESSION_WAIT_PUMP = 3
} waste_session_wait;

typedef struct {
    uint32_t record_size, abi_major, abi_minor, required_features;
    uint32_t max_input_bytes, max_output_bytes, timeout_ms, quantum_ms;
    /* Aggregate retained resident 64 KiB linear-memory pages, process slots
     * (including unreaped children), and file backing bytes. Zero instructions
     * disables that quota; the other limits must be nonzero. */
    uint32_t max_memory_pages, max_processes, max_filesystem_bytes;
    uint64_t max_instructions;
    /* Required monotonic clock; realtime and entropy may be omitted. Entropy
     * returns 0 on success, negative guest errno on failure. Callbacks receive
     * host-owned memory and must remain valid until session destruction. */
    uint64_t (*monotonic_ns)(void *);
    uint64_t (*realtime_ns)(void *);
    int32_t (*random_bytes)(void *, void *, uint32_t);
    void *host_context;
} waste_session_options;

typedef struct {
    uint32_t record_size, state, wait;
    int32_t exit_code; /* Valid only for EXITED; otherwise -1. */
    uint32_t signal, stdout_bytes, stderr_bytes;
    uint64_t elapsed_ns, wait_ns; /* UINT64_MAX means no timer deadline. */
    char message[256];
    uint32_t memory_pages_peak, filesystem_bytes_peak, processes_peak;
    uint64_t instructions;
} waste_session_result;

typedef struct {
    uint32_t kind, mode; /* regular=1, directory=2, symlink=3, null=4 */
    uint64_t size;
    int64_t mtime_sec, mtime_nsec;
} waste_session_file_info;

/* Initialize defaults, then supply monotonic_ns. Unsupported ABI/features and
 * invalid limits fail before creating a session. NULL destroy is harmless;
 * repeated destruction through delete(&session) is safe. */
void waste_session_options_init(waste_session_options *);
waste_status waste_session_create(const waste_session_options *, waste_session **,
                                   waste_error *);
void waste_session_delete(waste_session **);
/* Files/directories use canonical absolute UTF-8 paths. Parents must exist.
 * Population/cwd changes are allowed only before start. Existing files may be
 * replaced; the mode is permission bits only. The initial cwd is /work. */
waste_status waste_session_put_file(waste_session *, const char *,
                                    const uint8_t *, uint32_t, uint32_t mode,
                                    waste_error *);
waste_status waste_session_mkdir(waste_session *, const char *, uint32_t mode,
                                 waste_error *);
waste_status waste_session_set_cwd(waste_session *, const char *, waste_error *);
/* Append input before or during execution; eof permanently closes it after
 * queued bytes. Input defaults to an open empty stream. Limits count all input
 * bytes supplied and all captured output bytes across both output streams. */
waste_status waste_session_input(waste_session *, const uint8_t *, uint32_t,
                                 uint32_t eof, waste_error *);
/* args excludes argv[0], which is constructed from executable. env is a complete
 * NAME=VALUE vector; NULL selects documented defaults. Startup copies vectors
 * and initializes providers, but step executes the application's entry. */
waste_status waste_session_start(waste_session *, const char *executable,
                                 const char *const *args, uint32_t argc,
                                 const char *const *env, uint32_t envc,
                                 waste_error *);
waste_status waste_session_step(waste_session *, waste_error *);
void waste_session_cancel(waste_session *);
waste_status waste_session_inspect(const waste_session *, waste_session_result *);
/* Copy APIs retain no caller memory. Output and files survive terminal outcomes
 * until delete. File inspection is allowed before start or after completion.
 * read_file copies a bounded range; list returns immediate children, one per
 * index, and WASTE_ERROR_NOT_FOUND at the end. Returned names are basenames. */
waste_status waste_session_copy_output(const waste_session *, uint32_t fd,
    uint32_t offset, uint8_t *, uint32_t capacity, uint32_t *copied, waste_error *);
waste_status waste_session_stat(waste_session *, const char *,
                                waste_session_file_info *, waste_error *);
waste_status waste_session_read_file(waste_session *, const char *, uint64_t offset,
    uint8_t *, uint32_t capacity, uint32_t *copied, waste_error *);
waste_status waste_session_list(waste_session *, const char *, uint32_t index,
    char *name, uint32_t capacity, waste_session_file_info *, waste_error *);

#endif /* WASTE_H */
