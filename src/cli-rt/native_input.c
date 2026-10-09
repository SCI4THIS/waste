#define _XOPEN_SOURCE 700
#include "native_input.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Resolve from the actual executable, including symlink invocation. Never
 * select a filesystem just because the caller changed working directory. */
static int default_root(char *root, size_t capacity) {
    char location[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", location, sizeof(location) - 1);
    if (length <= 0 || (size_t)length >= sizeof(location) - 1) return 0;
    location[length] = 0;
    char *slash;
    while ((slash = strrchr(location, '/')) != NULL) {
        *slash = 0;
        char candidate[PATH_MAX];
        int count = snprintf(candidate, sizeof(candidate), "%s/src/vfs", location);
        if (count <= 0 || (size_t)count >= sizeof(candidate)) continue;
        struct stat info;
        if (!stat(candidate, &info) && S_ISDIR(info.st_mode)) {
            char canonical[PATH_MAX];
            if (!realpath(candidate, canonical) || strlen(canonical) >= capacity) return 0;
            memcpy(root, canonical, strlen(canonical) + 1);
            return 1;
        }
    }
    return 0;
}

int native_input_resolve_root(const char *override, char *root, size_t capacity) {
    char canonical[PATH_MAX];
    struct stat info;
    if ((override && !realpath(override, canonical)) ||
        (!override && !default_root(canonical, sizeof(canonical))) ||
        stat(canonical, &info) || !S_ISDIR(info.st_mode) || strlen(canonical) >= capacity)
        return -1;
    memcpy(root, canonical, strlen(canonical) + 1);
    return 0;
}

/* Open each canonical component without following a replacement symlink.
 * A supplied file grants only this file, never its parent directory tree. */
static int open_file(const char *canonical) {
    char components[PATH_MAX];
    memcpy(components, canonical, strlen(canonical) + 1);
    int directory = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) return -1;
    char *component = components + 1;
    for (;;) {
        char *slash = strchr(component, '/');
        if (slash) *slash = 0;
        int next = openat(directory, component, O_RDONLY | O_CLOEXEC | O_NOFOLLOW |
            (slash ? O_DIRECTORY : O_NONBLOCK));
        close(directory);
        if (next < 0 || !slash) return next;
        directory = next;
        component = slash + 1;
    }
}

void native_application_input_destroy(native_application_input *input) {
    free(input->bytes);
    memset(input, 0, sizeof(*input));
}
int native_application_input_open(native_application_input *input, const char *override,
    const char *file, native_exec_format format, char *error, size_t capacity) {
    memset(input, 0, sizeof(*input));
    char canonical[PATH_MAX];
    if ((override && !realpath(override, input->root)) ||
        (!override && !default_root(input->root, sizeof(input->root)))) {
        snprintf(error, capacity, "cannot resolve VFS root; use --vfs-root DIRECTORY");
        return -1;
    }
    struct stat root_info;
    if (stat(input->root, &root_info) || !S_ISDIR(root_info.st_mode)) {
        snprintf(error, capacity, "VFS root is not a directory");
        return -1;
    }
    if (!realpath(file, canonical)) {
        snprintf(error, capacity, "cannot resolve input file: %s", strerror(errno));
        return -1;
    }
    size_t root_length = strlen(input->root);
    const char *guest = NULL;
    if (root_length == 1) guest = canonical;
    else if (!strncmp(canonical, input->root, root_length) && canonical[root_length] == '/')
        guest = canonical + root_length;
    if (guest && strlen(guest) >= sizeof(input->guest)) {
        snprintf(error, capacity, "input guest path exceeds runtime limit");
        return -1;
    }
    int fd = open_file(canonical);
    struct stat before, after;
    if (fd < 0 || fstat(fd, &before) || !S_ISREG(before.st_mode) || before.st_size < 0 ||
        (uint64_t)before.st_size > NATIVE_EXEC_BYTES_MAX) {
        if (fd >= 0) close(fd);
        snprintf(error, capacity, "input must be a readable regular file within the executable byte limit");
        return -1;
    }
    if (guest) {
        memcpy(input->guest, guest, strlen(guest) + 1);
        close(fd);
        return 0; /* The mounted, confined tree supplies the actual bytes. */
    }
    input->staged = 1;
    input->mode = before.st_mode & 0777;
    snprintf(input->guest, sizeof(input->guest), "/tmp/waste-cli-input/program.%s",
             format == NATIVE_EXEC_FORMAT_WAT ? "wat" : "wasm");
    input->size = (size_t)before.st_size;
    input->bytes = malloc(input->size + 1);
    size_t at = 0;
    while (input->bytes && at < input->size) {
        ssize_t count = read(fd, input->bytes + at, input->size - at);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        at += (size_t)count;
    }
    uint8_t extra;
    ssize_t tail;
    do { tail = read(fd, &extra, 1); } while (tail < 0 && errno == EINTR);
    int failed = !input->bytes || at != input->size || tail != 0 || fstat(fd, &after) ||
        before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec;
    close(fd);
    if (failed) {
        native_application_input_destroy(input);
        snprintf(error, capacity, "cannot take a stable input file snapshot");
        return -1;
    }
    input->bytes[input->size] = 0;
    return 0;
}
