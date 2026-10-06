#include "posix_stubs.h"
#include "guest_posix.h"
#include "runtime_internal.h"
#include "lib/include/kernel.h"
#include "lib/include/select.h"
#include "lib/include/path.h"
#include "lib/include/sys/mman.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern void waste_browser_record_transition(const char *event);

/* Host imports for POSIX operations delegated to JavaScript */

__attribute__((import_module("waste_host"), import_name("posix_open")))
extern int32_t waste_host_posix_open(const char *path, size_t length,
                                     int32_t flags, int32_t mode);

__attribute__((import_module("waste_host"), import_name("posix_close")))
extern int32_t waste_host_posix_close(int32_t descriptor);

__attribute__((import_module("waste_host"), import_name("posix_read")))
extern int32_t waste_host_posix_read(int32_t descriptor, void *buffer,
                                     uint32_t count);

__attribute__((import_module("waste_host"), import_name("posix_write")))
extern int32_t waste_host_posix_write(int32_t descriptor, const void *buffer,
                                      uint32_t count);

__attribute__((import_module("waste_host"), import_name("wall_clock_ms")))
extern double waste_host_wall_clock_ms(void);

uint64_t waste_browser_realtime_now(void *data) {
    double milliseconds = waste_host_wall_clock_ms();
    (void)data;
    if (!(milliseconds > 0.0)) return 0;
    if (milliseconds >= (double)UINT64_MAX / 1000000.0) return UINT64_MAX;
    return (uint64_t)(milliseconds * 1000000.0);
}

/* Browser capabilities only; the shared resolver owns guest POSIX policy,
 * including the engine-owned host upload/download yields. */
static int32_t browser_open(void *data, const char *path, size_t length, int32_t flags, int32_t mode) {
    (void)data; return waste_host_posix_open(path, length, flags, mode);
}
static int32_t browser_close(void *data, int32_t fd) {
    (void)data; return waste_host_posix_close(fd);
}
static int32_t browser_read(void *data, int32_t fd, void *bytes, uint32_t length) {
    (void)data; return waste_host_posix_read(fd, bytes, length);
}
static int32_t browser_write(void *data, int32_t fd, const void *bytes, uint32_t length) {
    (void)data; return waste_host_posix_write(fd, bytes, length);
}
static void browser_trace(void *data, const char *event) {
    (void)data; waste_browser_record_transition(event);
}
static const guest_posix_platform browser_platform = {
    browser_open, browser_close, browser_read, browser_write, browser_trace
};
int browser_host_resolver(const char *module, const char *name, void *context, native_host_binding *out) {
    native_store *store = context;
    store->guest_platform = &browser_platform;
    store->guest_platform_data = NULL;
    return guest_posix_host_resolver(module, name, context, out);
}
