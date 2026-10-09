/* dlfcn.c — Dynamic loading for the WASTE guest libc.
 * Delegates to waste_kernel host functions which drive the engine's
 * shared-library loader (native_store_load_library and friends). */

#include "include/helper.h"

/* waste_kernel host imports for dynamic loading. */
__attribute__((import_module("waste_kernel"), import_name("dlopen_v1")))
extern i32 __waste_dlopen(const char *path, u32 path_len, i32 flags);

__attribute__((import_module("waste_kernel"), import_name("dlsym_v1")))
extern i32 __waste_dlsym(i32 handle, const char *name, u32 name_len);

__attribute__((import_module("waste_kernel"), import_name("dlclose_v1")))
extern i32 __waste_dlclose(i32 handle);

static char dl_error_buf[128];
static i32  dl_error_set;

void *dlopen(const char *path, i32 flags) {
    u32 len;
    i32 handle;
    if (!path) { dl_error_set = 1; return (void *)0; }
    len = c_length(path);
    if (len == 0) { dl_error_set = 1; return (void *)0; }
    handle = __waste_dlopen(path, len, flags);
    if (handle <= 0) {
        dl_error_set = 1;
        return (void *)0;
    }
    dl_error_set = 0;
    return (void *)(u32)handle;
}

void *dlsym(void *handle, const char *name) {
    u32 name_len;
    i32 result;
    if (!handle || !name) { dl_error_set = 1; return (void *)0; }
    name_len = c_length(name);
    if (name_len == 0) { dl_error_set = 1; return (void *)0; }
    result = __waste_dlsym((i32)(u32)handle, name, name_len);
    if (result == 0) {
        dl_error_set = 1;
        return (void *)0;
    }
    dl_error_set = 0;
    return (void *)(u32)result;
}

i32 dlclose(void *handle) {
    i32 status;
    if (!handle) return -1;
    status = __waste_dlclose((i32)(u32)handle);
    if (status != 0) {
        dl_error_set = 1;
        *__errno_location() = 22; /* EINVAL */
        return -1;
    }
    dl_error_set = 0;
    return 0;
}

char *dlerror(void) {
    if (!dl_error_set) return (char *)0;
    dl_error_set = 0;
    /* The engine does not currently propagate error text back to the
     * guest; return a generic message. */
    static const char msg[] = "dynamic loading failed";
    return (char *)msg;
}
