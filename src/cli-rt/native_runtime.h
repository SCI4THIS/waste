#ifndef WASTE_NATIVE_RUNTIME_H
#define WASTE_NATIVE_RUNTIME_H
#include "process_driver.h"
#include "native_wast.h"
#include "vfs.h"
#include <stdint.h>

typedef struct cli_guest_session native_runtime;
typedef struct {
    unsigned timeout_ms, cancel_after_ms, columns, rows;
    int control_fd, trace_waits, trace_process;
    int merge_output, report_default, raw_output, diagnostic_output, empty_descriptors, disable_test_suite;
    int clock_realtime_fixed, clock_monotonic_fixed;
    int host_stdio;
    uint64_t clock_realtime_ns, clock_monotonic_ns;
} native_runtime_options;

/* Platform adapter shared by application frontends and the private session
 * harness. Guest paths and process/library state stay inside the C engine. */
native_runtime *native_runtime_create(const char *root, const native_runtime_options *);
native_runtime *native_runtime_create_vfs(const char *root, const waste_vfs *, const native_runtime_options *);
native_store *native_runtime_store(native_runtime *);
int native_runtime_wast(native_runtime *, const char *, const char *, size_t,
    native_wast_counts *, const native_wast_options *);
int native_runtime_stage_file(native_runtime *, const char *guest, unsigned mode, const char *host);
int native_runtime_stage_input(native_runtime *, const char *guest, const uint8_t *, size_t, unsigned mode);
int native_runtime_queue_reply(native_runtime *, native_host_io_kind, int cancel, const char *host);
void native_runtime_script(native_runtime *, const char *host_script);
void native_runtime_start(native_runtime *, const char *guest,
    const char *const *argv, uint32_t argc, const char *const *envp, uint32_t envc,
    const native_process_start_options *);
/* Report is optional and never overwrites an existing file; finish releases
 * the runtime and returns its exit/error/timeout/cancellation status. */
int native_runtime_finish(native_runtime *, const char *report);
int native_runtime_failed(const native_runtime *);
#endif
