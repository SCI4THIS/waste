#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "../../config.h"

extern int waste_kernel_realpath_v1(const char *, char *, size_t)
    __attribute__((import_module("waste_kernel"), import_name("realpath_v1")));

char *realpath(const char *path, char *resolved) {
    int saved_errno = errno;
    char temporary[VFS_PATH_MAX_BYTES];
    int length = waste_kernel_realpath_v1(
        path, resolved ? resolved : temporary, VFS_PATH_MAX_BYTES);
    if (length < 0) {
        errno = -length;
        return NULL;
    }
    if (!resolved) {
        resolved = malloc((size_t)length + 1);
        if (!resolved) {
            errno = ENOMEM;
            return NULL;
        }
        memcpy(resolved, temporary, (size_t)length + 1);
    }
    errno = saved_errno;
    return resolved;
}
