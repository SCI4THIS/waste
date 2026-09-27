#include "include/kernel.h"
#include "include/select.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* --- Internal helpers --- */

static posix_ofd *ofd_alloc(posix_ofd_kind kind) {
    posix_ofd *ofd = calloc(1, sizeof(posix_ofd));
    if (!ofd) return NULL;
    ofd->kind = kind;
    ofd->ref_count = 1;
    return ofd;
}

/* Decrement the pipe endpoint count for this OFD kind.  Called once per
   fd close (not once per OFD destruction) so that the pipe's reader/writer
   tallies track open descriptors, not OFD objects. */
static void pipe_count_dec(posix_ofd *ofd) {
    if (!ofd || (ofd->kind != POSIX_OFD_PIPE_READ &&
                 ofd->kind != POSIX_OFD_PIPE_WRITE)) return;
    if (ofd->kind == POSIX_OFD_PIPE_READ) ofd->pipe->readers--;
    else if (ofd->kind == POSIX_OFD_PIPE_WRITE) ofd->pipe->writers--;
}

static void ofd_release(posix_ofd *ofd) {
    if (!ofd || --ofd->ref_count > 0) return;
    switch (ofd->kind) {
    case POSIX_OFD_TERMINAL:
        free(ofd->terminal.input);
        break;
    case POSIX_OFD_REGULAR:
        posix_kernel_file_release(ofd->regular.file);
        break;
    case POSIX_OFD_DIRECTORY:
        break;
    case POSIX_OFD_PIPE_READ:
    case POSIX_OFD_PIPE_WRITE:
        if (ofd->pipe->readers == 0 && ofd->pipe->writers == 0) {
            free(ofd->pipe->buffer);
            free(ofd->pipe);
        }
        break;
    }
    free(ofd);
}

static posix_file_object *file_object_create(const uint8_t *data,
                                             size_t length, uint64_t inode) {
    posix_file_object *object = calloc(1, sizeof(*object));
    if (!object) return NULL;
    object->refs = 1;
    object->inode = inode;
    if (length) {
        object->data = malloc(length);
        if (!object->data) { free(object); return NULL; }
        memcpy(object->data, data, length);
        object->data_capacity = length;
    }
    return object;
}

void posix_file_object_retain(posix_file_object *object) {
    if (object && object->refs != UINT32_MAX) object->refs++;
}

void posix_kernel_file_release(posix_file_object *object) {
    if (!object || !object->refs || --object->refs > 0) return;
    free(object->data);
    free(object);
}

posix_shm_namespace *posix_shm_namespace_create(void) {
    posix_shm_namespace *namespace_ = calloc(1, sizeof(*namespace_));
    if (namespace_) namespace_->refs = 1;
    return namespace_;
}

void posix_shm_namespace_retain(posix_shm_namespace *namespace_) {
    if (namespace_ && namespace_->refs != UINT32_MAX) namespace_->refs++;
}

void posix_shm_namespace_release(posix_shm_namespace *namespace_) {
    if (!namespace_ || !namespace_->refs || --namespace_->refs > 0) return;
    for (uint32_t i = 0; i < namespace_->count; i++)
        posix_kernel_file_release(namespace_->objects[i].file);
    free(namespace_);
}

posix_shm_namespace *posix_shm_namespace_clone(
        const posix_shm_namespace *source) {
    posix_shm_namespace *clone;
    if (!source) return NULL;
    clone = posix_shm_namespace_create();
    if (!clone) return NULL;
    clone->count = source->count;
    for (uint32_t i = 0; i < source->count; i++) {
        clone->objects[i] = source->objects[i];
        clone->objects[i].file = file_object_create(
            source->objects[i].file->data,
            source->objects[i].file->data_capacity,
            source->objects[i].file->inode);
        if (!clone->objects[i].file) {
            posix_shm_namespace_release(clone);
            return NULL;
        }
    }
    return clone;
}

static int kernel_find_free_fd(posix_kernel *k, int from) {
    for (int i = from; i < POSIX_KERNEL_FD_MAX; i++)
        if (!k->fds[i].ofd) return i;
    return -POSIX_EMFILE;
}

static int fd_valid(int fd) {
    return fd >= 0 && fd < POSIX_KERNEL_FD_MAX;
}

static int terminal_wait_with_timeout(posix_kernel *kernel, int fd,
                                      uint8_t vtime) {
    int result = posix_kernel_wait_read(kernel, fd);
    if (result == -POSIX_EAGAIN && vtime > 0 && kernel->clock_now &&
        !kernel->wait.has_deadline) {
        uint64_t now = kernel->clock_now(kernel->clock_data);
        uint64_t duration = (uint64_t)vtime * UINT64_C(100000000);
        kernel->wait.has_deadline = 1;
        kernel->wait.deadline_ns = now > UINT64_MAX - duration ?
            UINT64_MAX : now + duration;
    }
    return result;
}

/* --- Engine-owned pathname namespace --- */

static size_t bounded_length(const char *text) {
    if (!text) return 0;
    size_t length = 0;
    while (length < POSIX_PATH_NODE_NAME_MAX && text[length]) length++;
    if (length == POSIX_PATH_NODE_NAME_MAX && text[length]) return length + 1;
    return length;
}

static int path_normalize(const posix_kernel *kernel, const uint8_t *path,
                          size_t length, char *out) {
    if (!kernel || !path || !out || length == 0 || length >= POSIX_PATH_MAX)
        return -POSIX_EINVAL;
    size_t cwd_length = bounded_length(kernel->cwd);
    if (cwd_length == 0 || cwd_length >= POSIX_PATH_MAX) return -POSIX_EINVAL;
    char combined[POSIX_PATH_MAX * 2];
    size_t combined_length = 0;
    if (path[0] != '/') {
        if (cwd_length + 1 + length >= sizeof(combined)) return -POSIX_EINVAL;
        memcpy(combined, kernel->cwd, cwd_length);
        combined[cwd_length] = '/';
        combined_length = cwd_length + 1;
    }
    if (combined_length + length >= sizeof(combined)) return -POSIX_EINVAL;
    for (size_t i = 0; i < length; i++) {
        if (path[i] == 0) return -POSIX_EINVAL;
        combined[combined_length++] = (char)path[i];
    }
    size_t segment_start[POSIX_PATH_MAX / 2];
    size_t segment_length[POSIX_PATH_MAX / 2];
    size_t segments = 0;
    size_t at = 0;
    while (at < combined_length) {
        while (at < combined_length && combined[at] == '/') at++;
        size_t start = at;
        while (at < combined_length && combined[at] != '/') at++;
        size_t part_length = at - start;
        if (part_length == 0 || (part_length == 1 && combined[start] == '.')) continue;
        if (part_length == 2 && combined[start] == '.' && combined[start + 1] == '.') {
            if (segments > 0) segments--;
            continue;
        }
        if (segments >= sizeof(segment_start) / sizeof(segment_start[0]) ||
            part_length >= POSIX_PATH_NODE_NAME_MAX) return -POSIX_EINVAL;
        segment_start[segments] = start;
        segment_length[segments] = part_length;
        segments++;
    }
    size_t out_length = 1;
    out[0] = '/';
    for (size_t i = 0; i < segments; i++) {
        if (out_length > 1) out[out_length++] = '/';
        if (out_length + segment_length[i] >= POSIX_PATH_NODE_NAME_MAX)
            return -POSIX_EINVAL;
        memcpy(out + out_length, combined + segment_start[i], segment_length[i]);
        out_length += segment_length[i];
    }
    out[out_length] = 0;
    return 0;
}

static posix_kernel_path_node *path_find(posix_kernel *kernel,
                                         const char *normalized) {
    for (int i = 0; i < kernel->path_node_count; i++)
        if (strcmp(kernel->path_nodes[i].path, normalized) == 0)
            return &kernel->path_nodes[i];
    return NULL;
}

static int path_prefix_is_file(const posix_kernel *kernel, const char *path) {
    char prefix[POSIX_PATH_NODE_NAME_MAX];
    size_t length = strlen(path);
    for (size_t at = 1; at < length; at++) {
        if (path[at] != '/') continue;
        if (at >= sizeof(prefix)) return 0;
        memcpy(prefix, path, at);
        prefix[at] = 0;
        for (int i = 0; i < kernel->path_node_count; i++) {
            const posix_kernel_path_node *node = &kernel->path_nodes[i];
            if (strcmp(node->path, prefix) == 0 &&
                node->metadata.kind != POSIX_NODE_DIRECTORY)
                return 1;
        }
    }
    return 0;
}

int posix_kernel_path_add(posix_kernel *kernel, const char *path,
                          const posix_path_metadata *metadata) {
    return posix_kernel_path_add_data(kernel, path, metadata, NULL, 0);
}

int posix_kernel_path_add_data(posix_kernel *kernel, const char *path,
                               const posix_path_metadata *metadata,
                               const uint8_t *data, size_t data_length) {
    if (!kernel || !path || !metadata ||
        posix_path_metadata_validate(metadata) < 0) return -POSIX_EINVAL;
    if (metadata->kind != POSIX_NODE_REGULAR && data_length != 0)
        return -POSIX_EINVAL;
    if (data_length > 0 && !data) return -POSIX_EFAULT;
    size_t length = bounded_length(path);
    if (length == 0 || length >= POSIX_PATH_MAX) return -POSIX_EINVAL;
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    int result = path_normalize(kernel, (const uint8_t *)path, length, normalized);
    if (result < 0) return result;
    posix_kernel_path_node *existing = path_find(kernel, normalized);
    if (existing) {
        posix_file_object *file = metadata->kind == POSIX_NODE_REGULAR ?
            file_object_create(data, data_length, metadata->inode) : NULL;
        if (metadata->kind == POSIX_NODE_REGULAR && !file)
            return -POSIX_ENOMEM;
        posix_kernel_file_release(existing->file);
        free(existing->link_target);
        existing->link_target = NULL;
        existing->file = file;
        existing->metadata = *metadata;
        if (metadata->kind == POSIX_NODE_REGULAR && data_length > 0)
            existing->metadata.size = (int64_t)data_length;
        return 0;
    }
    if (kernel->path_node_count >= POSIX_PATH_NODE_MAX) return -POSIX_ENOMEM;
    existing = &kernel->path_nodes[kernel->path_node_count++];
    memset(existing, 0, sizeof(*existing));
    memcpy(existing->path, normalized, strlen(normalized) + 1);
    existing->metadata = *metadata;
    if (metadata->kind == POSIX_NODE_REGULAR) {
        existing->file = file_object_create(data, data_length,
                                            metadata->inode);
        if (!existing->file) {
            kernel->path_node_count--;
            return -POSIX_ENOMEM;
        }
    }
    if (metadata->kind == POSIX_NODE_REGULAR && data_length > 0)
        existing->metadata.size = (int64_t)data_length;
    return 0;
}

int posix_kernel_path_add_symlink(posix_kernel *kernel, const char *path,
                                  const posix_path_metadata *metadata,
                                  const char *target) {
    if (!kernel || !target || !metadata || metadata->kind != POSIX_NODE_SYMLINK)
        return -POSIX_EINVAL;
    int result = posix_kernel_path_add_data(kernel, path, metadata, NULL, 0);
    if (result < 0) return result;
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    size_t length = bounded_length(path);
    if (length == 0 || length >= POSIX_PATH_MAX ||
        path_normalize(kernel, (const uint8_t *)path, length, normalized) < 0)
        return -POSIX_EINVAL;
    posix_kernel_path_node *node = path_find(kernel, normalized);
    size_t target_length = bounded_length(target);
    if (!node || target_length == 0 || target_length >= POSIX_PATH_NODE_NAME_MAX)
        return -POSIX_EINVAL;
    node->link_target = malloc(target_length + 1);
    if (!node->link_target) return -POSIX_ENOMEM;
    memcpy(node->link_target, target, target_length + 1);
    return 0;
}

int posix_kernel_getcwd(const posix_kernel *kernel, char *buffer, size_t capacity) {
    if (!kernel || !buffer || capacity == 0) return -POSIX_EINVAL;
    size_t length = strlen(kernel->cwd) + 1;
    if (length > capacity) return -POSIX_ERANGE;
    memcpy(buffer, kernel->cwd, length);
    return 0;
}

int posix_kernel_path_mkdir(posix_kernel *kernel, const uint8_t *path,
                            size_t length, int mode) {
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    if (!kernel || path_normalize(kernel, path, length, normalized) < 0)
        return -POSIX_EINVAL;
    if (path_find(kernel, normalized)) return -POSIX_EEXIST;
    posix_path_metadata metadata = { POSIX_NODE_DIRECTORY,
        (uint32_t)(mode ? mode : 0777), 0, 0, 0,
        1000u + (uint64_t)kernel->path_node_count };
    return posix_kernel_path_add_data(kernel, normalized, &metadata, NULL, 0);
}

int posix_kernel_path_unlink(posix_kernel *kernel, const uint8_t *path,
                             size_t length, int directory) {
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    if (!kernel || path_normalize(kernel, path, length, normalized) < 0)
        return -POSIX_EINVAL;
    posix_kernel_path_node *node = path_find(kernel, normalized);
    if (!node) return -POSIX_ENOENT;
    if (directory != (node->metadata.kind == POSIX_NODE_DIRECTORY))
        return directory ? -POSIX_ENOTDIR : -POSIX_EISDIR;
    if (directory) {
        size_t prefix = strlen(normalized);
        for (int i = 0; i < kernel->path_node_count; i++) {
            if (kernel->path_nodes[i].path[0] &&
                strncmp(kernel->path_nodes[i].path, normalized, prefix) == 0 &&
                kernel->path_nodes[i].path[prefix] == '/')
                return -POSIX_ENOTEMPTY;
        }
    }
    posix_kernel_file_release(node->file);
    free(node->link_target);
    int index = (int)(node - kernel->path_nodes);
    for (int i = index; i + 1 < kernel->path_node_count; i++)
        kernel->path_nodes[i] = kernel->path_nodes[i + 1];
    memset(&kernel->path_nodes[--kernel->path_node_count], 0,
           sizeof(kernel->path_nodes[0]));
    return 0;
}

int posix_kernel_path_rename(posix_kernel *kernel,
                             const uint8_t *source, size_t source_length,
                             const uint8_t *destination, size_t destination_length) {
    char source_name[POSIX_PATH_NODE_NAME_MAX];
    char destination_name[POSIX_PATH_NODE_NAME_MAX];
    if (!kernel || path_normalize(kernel, source, source_length, source_name) < 0 ||
        path_normalize(kernel, destination, destination_length, destination_name) < 0)
        return -POSIX_EINVAL;
    posix_kernel_path_node *from = path_find(kernel, source_name);
    posix_kernel_path_node *to = path_find(kernel, destination_name);
    if (!from) return -POSIX_ENOENT;
    if (to) {
        if (to->metadata.kind == POSIX_NODE_DIRECTORY) return -POSIX_EISDIR;
        posix_kernel_file_release(to->file); free(to->link_target);
        *to = *from;
        from->file = NULL; from->link_target = NULL;
        memset(from, 0, sizeof(*from));
        return 0;
    }
    size_t length = strlen(destination_name);
    memcpy(from->path, destination_name, length + 1);
    return 0;
}

int posix_kernel_path_readlink(posix_kernel *kernel, const uint8_t *path,
                               size_t length, char *buffer, size_t capacity) {
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    if (!kernel || !buffer || capacity == 0 ||
        path_normalize(kernel, path, length, normalized) < 0)
        return -POSIX_EINVAL;
    posix_kernel_path_node *node = path_find(kernel, normalized);
    if (!node) return -POSIX_ENOENT;
    if (node->metadata.kind != POSIX_NODE_SYMLINK || !node->link_target)
        return -POSIX_EINVAL;
    size_t target_length = strlen(node->link_target);
    if (target_length > capacity) target_length = capacity;
    memcpy(buffer, node->link_target, target_length);
    return (int)target_length;
}

int posix_kernel_path_set_cwd(posix_kernel *kernel, const char *path) {
    if (!kernel || !path) return -POSIX_EINVAL;
    size_t length = bounded_length(path);
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    int result = path_normalize(kernel, (const uint8_t *)path, length, normalized);
    if (result < 0) return result;
    posix_kernel_path_node *node = path_find(kernel, normalized);
    if (!node) return path_prefix_is_file(kernel, normalized) ? -POSIX_ENOTDIR : -POSIX_ENOENT;
    if (node->metadata.kind != POSIX_NODE_DIRECTORY) return -POSIX_ENOTDIR;
    memcpy(kernel->cwd, normalized, strlen(normalized) + 1);
    return 0;
}

int posix_kernel_path_stat(posix_kernel *kernel, const uint8_t *path,
                           size_t length, int follow,
                           posix_path_metadata *metadata) {
    (void)follow;
    if (!kernel || !metadata) return -POSIX_EFAULT;
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    int result = path_normalize(kernel, path, length, normalized);
    if (result < 0) return result;
    posix_kernel_path_node *node = path_find(kernel, normalized);
    if (!node) return path_prefix_is_file(kernel, normalized) ? -POSIX_ENOTDIR : -POSIX_ENOENT;
    if (follow) {
        int depth = 0;
        while (node && node->metadata.kind == POSIX_NODE_SYMLINK &&
               node->link_target && depth++ < 8) {
            if (path_normalize(kernel, (const uint8_t *)node->link_target,
                               strlen(node->link_target), normalized) < 0)
                return -POSIX_EINVAL;
            node = path_find(kernel, normalized);
        }
        if (!node) return -POSIX_ENOENT;
        if (node->metadata.kind == POSIX_NODE_SYMLINK) return -POSIX_ELOOP;
    }
    *metadata = node->metadata;
    if (node->metadata.kind == POSIX_NODE_REGULAR && node->file)
        metadata->size = (int64_t)node->file->data_capacity;
    return 0;
}

int posix_kernel_path_access(posix_kernel *kernel, const uint8_t *path,
                             size_t length, int mode, int flags) {
    if (flags != 0 || (mode & ~(POSIX_F_OK | POSIX_X_OK | POSIX_W_OK | POSIX_R_OK)))
        return -POSIX_EINVAL;
    posix_path_metadata metadata;
    int result = posix_kernel_path_stat(kernel, path, length, 1, &metadata);
    if (result < 0 || mode == POSIX_F_OK) return result;
    int permissions = 0;
    if (metadata.mode & 0444) permissions |= POSIX_R_OK;
    if (metadata.mode & 0222) permissions |= POSIX_W_OK;
    if (metadata.mode & 0111) permissions |= POSIX_X_OK;
    return (permissions & mode) == mode ? 0 : -POSIX_EACCES;
}

int posix_kernel_path_snapshot(posix_kernel *kernel, const uint8_t *path,
                               size_t length, size_t maximum_size,
                               uint8_t **data_out, size_t *length_out,
                               posix_path_metadata *metadata_out) {
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    posix_kernel_path_node *node;
    uint8_t *copy = NULL;
    int depth = 0;
    int result;
    if (!kernel || !path || !data_out || !length_out || !metadata_out)
        return -POSIX_EFAULT;
    *data_out = NULL;
    *length_out = 0;
    result = path_normalize(kernel, path, length, normalized);
    if (result < 0) return result;
    node = path_find(kernel, normalized);
    if (!node)
        return path_prefix_is_file(kernel, normalized) ?
            -POSIX_ENOTDIR : -POSIX_ENOENT;
    while (node && node->metadata.kind == POSIX_NODE_SYMLINK &&
           node->link_target && depth++ < 8) {
        result = path_normalize(kernel, (const uint8_t *)node->link_target,
                                strlen(node->link_target), normalized);
        if (result < 0) return result;
        node = path_find(kernel, normalized);
    }
    if (!node) return -POSIX_ENOENT;
    if (node->metadata.kind == POSIX_NODE_SYMLINK)
        return -POSIX_ELOOP;
    if (node->metadata.kind == POSIX_NODE_DIRECTORY)
        return -POSIX_EISDIR;
    if (node->metadata.kind != POSIX_NODE_REGULAR)
        return -POSIX_ENOENT;
    if (!(node->metadata.mode & 0111u)) return -POSIX_EACCES;
    if (maximum_size != 0 && node->file->data_capacity > maximum_size)
        return -POSIX_E2BIG;
    if (node->file->data_capacity > 0) {
        copy = (uint8_t *)malloc(node->file->data_capacity);
        if (!copy) return -POSIX_ENOMEM;
        memcpy(copy, node->file->data, node->file->data_capacity);
    }
    *data_out = copy;
    *length_out = node->file->data_capacity;
    *metadata_out = node->metadata;
    metadata_out->size = (int64_t)node->file->data_capacity;
    return 0;
}

/* --- Lifecycle --- */

posix_kernel *posix_kernel_create(int interactive) {
    posix_kernel *k = calloc(1, sizeof(posix_kernel));
    if (!k) return NULL;
    k->shm_namespace = posix_shm_namespace_create();
    if (!k->shm_namespace) { free(k); return NULL; }

    memcpy(k->cwd, "/", 2);
    const posix_path_metadata root = { POSIX_NODE_DIRECTORY, 0755, 0, 0, 0, 1 };
    const posix_path_metadata directory = { POSIX_NODE_DIRECTORY, 0755, 0, 0, 0, 2 };
    if (posix_kernel_path_add(k, "/", &root) < 0 ||
        posix_kernel_path_add(k, "/bin", &directory) < 0 ||
        posix_kernel_path_add(k, "/usr", &directory) < 0 ||
        posix_kernel_path_add(k, "/usr/bin", &directory) < 0) {
        posix_shm_namespace_release(k->shm_namespace);
        free(k);
        return NULL;
    }

    if (interactive) {
        posix_ofd *tty = ofd_alloc(POSIX_OFD_TERMINAL);
        if (!tty) { posix_shm_namespace_release(k->shm_namespace); free(k); return NULL; }
        tty->terminal.input = malloc(POSIX_TERMINAL_INPUT_CAPACITY);
        if (!tty->terminal.input) { free(tty); posix_shm_namespace_release(k->shm_namespace); free(k); return NULL; }
        tty->terminal.input_length = 0;
        tty->terminal.input_capacity = POSIX_TERMINAL_INPUT_CAPACITY;
        tty->terminal.eof = 0;
        /* Preserve the byte-queue contract used by existing native probes.
         * Interactive libc will select canonical/echo mode through
         * tcsetattr; keeping creation raw makes the kernel useful before a
         * process has installed its terminal policy. */
        tty->terminal.termios.iflag = 0;
        tty->terminal.termios.lflag = 0;
        tty->terminal.termios.cc[POSIX_TERMIOS_VINTR] = 3;
        tty->terminal.termios.cc[POSIX_TERMIOS_VEOF] = 4;
        tty->terminal.termios.cc[POSIX_TERMIOS_VERASE] = 127;
        tty->terminal.termios.cc[POSIX_TERMIOS_VKILL] = 21;
        tty->terminal.termios.cc[POSIX_TERMIOS_VMIN] = 1;
        tty->terminal.termios.cc[POSIX_TERMIOS_VTIME] = 0;
        tty->terminal.winsize.rows = 24;
        tty->terminal.winsize.columns = 80;
        tty->terminal.foreground_pgid = 1;
        /* fds 0, 1, 2 share the same terminal OFD */
        tty->ref_count = 3;
        k->fds[0].ofd = tty;
        k->fds[1].ofd = tty;
        k->fds[2].ofd = tty;
    }
    k->process_group_id = 1;
    return k;
}

void posix_kernel_destroy(posix_kernel *kernel);
posix_kernel *posix_kernel_clone(const posix_kernel *source);

posix_kernel *posix_kernel_clone(const posix_kernel *source) {
    if (!source) return NULL;
    posix_kernel *clone = calloc(1, sizeof(*clone));
    if (!clone) return NULL;
    memcpy(clone, source, sizeof(*clone));
    posix_shm_namespace_retain(clone->shm_namespace);
    for (int i = 0; i < clone->path_node_count; i++) {
        posix_file_object_retain(clone->path_nodes[i].file);
        if (source->path_nodes[i].link_target) {
            size_t length = strlen(source->path_nodes[i].link_target);
            clone->path_nodes[i].link_target = malloc(length + 1);
            if (!clone->path_nodes[i].link_target) {
                posix_kernel_destroy(clone);
                return NULL;
            }
            memcpy(clone->path_nodes[i].link_target,
                   source->path_nodes[i].link_target, length + 1);
        }
    }
    memset(clone->fds, 0, sizeof(clone->fds));
    clone->wait.active = 0;
    const posix_ofd *regular_sources[POSIX_KERNEL_FD_MAX] = {0};
    posix_ofd *regular_clones[POSIX_KERNEL_FD_MAX] = {0};
    int regular_count = 0;
    for (int fd = 0; fd < POSIX_KERNEL_FD_MAX; fd++) {
        const posix_ofd *source_ofd = source->fds[fd].ofd;
        if (!source_ofd) continue;
        posix_ofd *shared = (posix_ofd *)source_ofd;
        if (shared->kind == POSIX_OFD_REGULAR) {
            ptrdiff_t index = shared->regular.node - source->path_nodes;
            if (index < 0 || index >= source->path_node_count) {
                posix_kernel_destroy(clone);
                return NULL;
            }
            /* Open descriptions are shared by fork and point at the
             * corresponding cloned pathname entry and shared file object. */
            posix_ofd *copy = NULL;
            for (int i = 0; i < regular_count; i++)
                if (regular_sources[i] == source_ofd) copy = regular_clones[i];
            if (!copy) {
                if (regular_count >= POSIX_KERNEL_FD_MAX) {
                    posix_kernel_destroy(clone); return NULL;
                }
                copy = malloc(sizeof(*copy));
                if (!copy) { posix_kernel_destroy(clone); return NULL; }
                *copy = *shared;
                copy->ref_count = 1;
                copy->regular.node = &clone->path_nodes[index];
                posix_file_object_retain(copy->regular.file);
                regular_sources[regular_count] = source_ofd;
                regular_clones[regular_count++] = copy;
            } else {
                copy->ref_count++;
            }
            clone->fds[fd].ofd = copy;
        } else {
            clone->fds[fd].ofd = shared;
            shared->ref_count++;
            if (shared->kind == POSIX_OFD_PIPE_READ) shared->pipe->readers++;
            if (shared->kind == POSIX_OFD_PIPE_WRITE) shared->pipe->writers++;
        }
    }
    return clone;
}

int posix_kernel_merge_paths(posix_kernel *target,
                             const posix_kernel *source) {
    if (!target || !source) return -POSIX_EINVAL;
    for (int i = 0; i < source->path_node_count; i++) {
        const posix_kernel_path_node *from = &source->path_nodes[i];
        posix_kernel_path_node *to = path_find(target, from->path);
        char *link_target = NULL;
        if (from->link_target) {
            size_t length = strlen(from->link_target);
            link_target = malloc(length + 1u);
            if (!link_target) return -POSIX_ENOMEM;
            memcpy(link_target, from->link_target, length + 1u);
        }
        if (!to) {
            if (target->path_node_count >= POSIX_PATH_NODE_MAX) {
                free(link_target);
                return -POSIX_ENOMEM;
            }
            to = &target->path_nodes[target->path_node_count++];
            memset(to, 0, sizeof(*to));
            memcpy(to->path, from->path, sizeof(to->path));
            to->file = from->file;
            posix_file_object_retain(to->file);
        } else if (to->file != from->file) {
            posix_kernel_file_release(to->file);
            to->file = from->file;
            posix_file_object_retain(to->file);
        }
        to->metadata = from->metadata;
        free(to->link_target);
        to->link_target = link_target;
    }
    return 0;
}

void posix_kernel_destroy(posix_kernel *kernel) {
    if (!kernel) return;
    for (int i = 0; i < POSIX_KERNEL_FD_MAX; i++) {
        posix_ofd *ofd = kernel->fds[i].ofd;
        if (ofd) {
            kernel->fds[i].ofd = NULL;
            pipe_count_dec(ofd);
            ofd_release(ofd);
        }
    }
    for (int i = 0; i < kernel->path_node_count; i++)
        { posix_kernel_file_release(kernel->path_nodes[i].file);
          free(kernel->path_nodes[i].link_target); }
    posix_shm_namespace_release(kernel->shm_namespace);
    free(kernel);
}

int posix_kernel_set_shm_namespace(posix_kernel *kernel,
                                    posix_shm_namespace *namespace_) {
    if (!kernel || !namespace_) return -POSIX_EINVAL;
    posix_shm_namespace_retain(namespace_);
    posix_shm_namespace_release(kernel->shm_namespace);
    kernel->shm_namespace = namespace_;
    return 0;
}

int posix_kernel_set_credentials(posix_kernel *kernel, uint32_t uid,
                                 uint32_t gid) {
    if (!kernel) return -POSIX_EINVAL;
    kernel->uid = uid;
    kernel->gid = gid;
    return 0;
}

void posix_kernel_set_clock(posix_kernel *kernel, posix_clock_now_fn clock_now,
                            void *clock_data) {
    if (!kernel) return;
    kernel->clock_now = clock_now;
    kernel->clock_data = clock_data;
}

static int signal_valid(int signal) {
    return signal >= 1 && signal <= POSIX_SIGNAL_MAX;
}

static void signal_bit_set(posix_sigset *set, int signal) {
    uint32_t bit = (uint32_t)(signal - 1);
    set->words[bit / 32] |= UINT32_C(1) << (bit % 32);
}

static void signal_bit_clear(posix_sigset *set, int signal) {
    uint32_t bit = (uint32_t)(signal - 1);
    set->words[bit / 32] &= ~(UINT32_C(1) << (bit % 32));
}

static int signal_bit_test(const posix_sigset *set, int signal) {
    uint32_t bit = (uint32_t)(signal - 1);
    return (set->words[bit / 32] >> (bit % 32)) & 1;
}

static int first_unmasked_pending(const posix_kernel *kernel) {
    for (int signal = 1; signal <= POSIX_SIGNAL_MAX; signal++)
        if (signal_bit_test(&kernel->pending_signals, signal) &&
            !signal_bit_test(&kernel->signal_mask, signal))
            return signal;
    return 0;
}

void posix_kernel_set_signal_mask(posix_kernel *kernel,
                                  const posix_sigset *mask) {
    if (!kernel || !mask) return;
    if (!kernel->wait.has_signal_mask) kernel->signal_mask = *mask;
}

void posix_kernel_get_signal_mask(const posix_kernel *kernel,
                                  posix_sigset *mask) {
    if (!kernel || !mask) return;
    *mask = kernel->signal_mask;
}

int posix_kernel_signal_raise(posix_kernel *kernel, int signal) {
    if (!kernel || !signal_valid(signal)) return -POSIX_EINVAL;
    if (kernel->signal_disposition[signal] == POSIX_SIGNAL_IGNORE)
        return 0;
    signal_bit_set(&kernel->pending_signals, signal);
    return 0;
}

int posix_kernel_signal_clear(posix_kernel *kernel, int signal) {
    if (!kernel || !signal_valid(signal)) return -POSIX_EINVAL;
    signal_bit_clear(&kernel->pending_signals, signal);
    return 0;
}

int posix_kernel_signal_pending(const posix_kernel *kernel, int signal) {
    if (!kernel || !signal_valid(signal)) return 0;
    return signal_bit_test(&kernel->pending_signals, signal);
}

int posix_kernel_signal_last_delivered(posix_kernel *kernel) {
    int signal;
    if (!kernel) return -POSIX_EINVAL;
    signal = kernel->delivered_signal;
    kernel->delivered_signal = 0;
    return signal;
}

int posix_kernel_signal_set_disposition(posix_kernel *kernel, int signal,
                                        posix_signal_disposition disposition) {
    if (!kernel || !signal_valid(signal) ||
        disposition < POSIX_SIGNAL_DEFAULT ||
        disposition > POSIX_SIGNAL_HANDLER)
        return -POSIX_EINVAL;
    if ((signal == POSIX_SIGKILL || signal == POSIX_SIGSTOP) &&
        disposition != POSIX_SIGNAL_DEFAULT)
        return -POSIX_EINVAL;
    kernel->signal_disposition[signal] = (uint8_t)disposition;
    if (disposition == POSIX_SIGNAL_IGNORE)
        signal_bit_clear(&kernel->pending_signals, signal);
    return 0;
}

int posix_kernel_signal_get_disposition(const posix_kernel *kernel, int signal,
                                        posix_signal_disposition *disposition) {
    if (!kernel || !signal_valid(signal) || !disposition)
        return -POSIX_EINVAL;
    *disposition = (posix_signal_disposition)kernel->signal_disposition[signal];
    return 0;
}

int posix_kernel_signal_set_handler(posix_kernel *kernel, int signal,
                                    uint32_t handler) {
    if (!kernel || !signal_valid(signal)) return -POSIX_EINVAL;
    kernel->signal_handlers[signal] = handler;
    return 0;
}

int posix_kernel_signal_get_handler(const posix_kernel *kernel, int signal,
                                    uint32_t *handler) {
    if (!kernel || !signal_valid(signal) || !handler) return -POSIX_EINVAL;
    *handler = kernel->signal_handlers[signal];
    return 0;
}

int posix_kernel_signal_set_action_mask(posix_kernel *kernel, int signal,
                                        const posix_sigset *mask) {
    if (!kernel || !signal_valid(signal) || !mask) return -POSIX_EINVAL;
    kernel->signal_action_masks[signal] = *mask;
    return 0;
}

int posix_kernel_signal_get_action_mask(const posix_kernel *kernel, int signal,
                                        posix_sigset *mask) {
    if (!kernel || !signal_valid(signal) || !mask) return -POSIX_EINVAL;
    *mask = kernel->signal_action_masks[signal];
    return 0;
}

int posix_kernel_signal_enter_handler(posix_kernel *kernel, int signal,
                                      posix_sigset *saved_mask) {
    posix_sigset action_mask;
    if (!kernel || !signal_valid(signal) || !saved_mask) return -POSIX_EINVAL;
    *saved_mask = kernel->signal_mask;
    if (posix_kernel_signal_get_action_mask(kernel, signal, &action_mask) < 0)
        return -POSIX_EINVAL;
    for (size_t i = 0; i < POSIX_SIGSET_BYTES / sizeof(uint32_t); i++)
        kernel->signal_mask.words[i] |= action_mask.words[i];
    signal_bit_set(&kernel->signal_mask, signal);
    return 0;
}

void posix_kernel_signal_leave_handler(posix_kernel *kernel,
                                       const posix_sigset *saved_mask) {
    if (kernel && saved_mask) kernel->signal_mask = *saved_mask;
}

int posix_kernel_getpgid(const posix_kernel *kernel) {
    return kernel ? kernel->process_group_id : -POSIX_EINVAL;
}

int posix_kernel_setpgid(posix_kernel *kernel, int pgid) {
    if (!kernel || pgid <= 0) return -POSIX_EINVAL;
    kernel->process_group_id = pgid;
    return pgid;
}

int posix_kernel_terminal_get_foreground_pgid(const posix_kernel *kernel,
                                              int fd) {
    if (!posix_kernel_isatty(kernel, fd)) return -POSIX_EBADF;
    return kernel->fds[fd].ofd->terminal.foreground_pgid;
}

int posix_kernel_terminal_set_foreground_pgid(posix_kernel *kernel, int fd,
                                              int pgid) {
    if (!posix_kernel_isatty(kernel, fd) || pgid <= 0) return -POSIX_EINVAL;
    kernel->fds[fd].ofd->terminal.foreground_pgid = pgid;
    return 0;
}

static void route_terminal_signals(posix_kernel *kernel) {
    if (!kernel || kernel->process_group_id <= 0) return;
    for (int fd = 0; fd < POSIX_KERNEL_FD_MAX; fd++) {
        posix_ofd *ofd = kernel->fds[fd].ofd;
        if (!ofd || ofd->kind != POSIX_OFD_TERMINAL ||
            ofd->terminal.foreground_pgid != kernel->process_group_id)
            continue;
        for (int signal = 1; signal <= POSIX_SIGNAL_MAX; signal++) {
            if (signal_bit_test(&ofd->terminal.pending_signals, signal)) {
                signal_bit_clear(&ofd->terminal.pending_signals, signal);
                (void)posix_kernel_signal_raise(kernel, signal);
            }
        }
        return;
    }
}

void posix_kernel_cancel_wait(posix_kernel *kernel) {
    if (!kernel) return;
    if (kernel->wait.has_signal_mask)
        kernel->signal_mask = kernel->wait.saved_mask;
    kernel->wait.active = 0;
    kernel->wait.has_signal_mask = 0;
}

int posix_kernel_wait_active(const posix_kernel *kernel) {
    return kernel && kernel->wait.active;
}

uint64_t posix_kernel_wait_generation(const posix_kernel *kernel) {
    return kernel ? kernel->wait.generation : 0;
}

static int wait_sets_ready(posix_kernel *kernel,
                           const posix_wait_record *wait) {
    int limit = wait->nfds < POSIX_KERNEL_FD_MAX ?
                wait->nfds : POSIX_KERNEL_FD_MAX;
    for (int fd = 0; fd < limit; fd++) {
        int interested = posix_fd_isset(fd, &wait->readfds) ||
                         posix_fd_isset(fd, &wait->writefds) ||
                         posix_fd_isset(fd, &wait->exceptfds);
        if (!interested) continue;
        int readiness = posix_kernel_query_readiness(kernel, fd);
        if (readiness < 0) continue;
        if (posix_fd_isset(fd, &wait->readfds) &&
            (readiness & (POSIX_POLL_IN | POSIX_POLL_HUP))) return 1;
        if (posix_fd_isset(fd, &wait->writefds) &&
            (readiness & POSIX_POLL_OUT)) return 1;
        if (posix_fd_isset(fd, &wait->exceptfds) &&
            (readiness & POSIX_POLL_ERR)) return 1;
    }
    return 0;
}

int posix_kernel_wait_poll(posix_kernel *kernel) {
    if (!kernel || !kernel->wait.active) return POSIX_WAIT_BLOCKED;
    route_terminal_signals(kernel);
    if (first_unmasked_pending(kernel)) return POSIX_WAIT_SIGNAL;
    if (wait_sets_ready(kernel, &kernel->wait)) return POSIX_WAIT_READY;
    if (kernel->wait.has_deadline && kernel->clock_now) {
        uint64_t now = kernel->clock_now(kernel->clock_data);
        if (now >= kernel->wait.deadline_ns) return POSIX_WAIT_TIMEOUT;
    }
    return POSIX_WAIT_BLOCKED;
}

int posix_kernel_wait_read(posix_kernel *kernel, int fd) {
    if (!kernel || !fd_valid(fd)) return -POSIX_EINVAL;
    int readiness = posix_kernel_query_readiness(kernel, fd);
    if (readiness < 0) return readiness;
    if (readiness & (POSIX_POLL_IN | POSIX_POLL_HUP)) {
        posix_kernel_cancel_wait(kernel);
        return 0;
    }
    if (kernel->wait.active) return -POSIX_EAGAIN;
    posix_fd_zero(&kernel->wait.readfds);
    posix_fd_set_bit(fd, &kernel->wait.readfds);
    posix_fd_zero(&kernel->wait.writefds);
    posix_fd_zero(&kernel->wait.exceptfds);
    kernel->wait.nfds = fd + 1;
    kernel->wait.has_deadline = 0;
    kernel->wait.has_signal_mask = 0;
    kernel->wait.generation++;
    if (kernel->wait.generation == 0) kernel->wait.generation = 1;
    kernel->wait.active = 1;
    return -POSIX_EAGAIN;
}

/* --- Readiness --- */

int posix_kernel_query_readiness(posix_kernel *kernel, int fd) {
    if (!kernel || !fd_valid(fd)) return -POSIX_EINVAL;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;

    int mask = 0;
    switch (ofd->kind) {
    case POSIX_OFD_TERMINAL:
        if (ofd->terminal.eof || ofd->terminal.input_length > 0) {
            int readable = !(ofd->terminal.termios.lflag & POSIX_TERMIOS_LFLAG_ICANON);
            if (ofd->terminal.termios.lflag & POSIX_TERMIOS_LFLAG_ICANON) {
                for (int i = 0; i < ofd->terminal.input_length; i++) {
                    if (ofd->terminal.input[i] == '\n') { readable = 1; break; }
                }
            }
            if (readable) mask |= POSIX_POLL_IN;
        }
        if (ofd->terminal.eof)
            mask |= POSIX_POLL_HUP;
        mask |= POSIX_POLL_OUT; /* terminal output is always ready */
        break;
    case POSIX_OFD_PIPE_READ:
        if (ofd->pipe->length > 0)
            mask |= POSIX_POLL_IN;
        if (ofd->pipe->writers == 0) {
            mask |= POSIX_POLL_IN | POSIX_POLL_HUP;
        }
        break;
    case POSIX_OFD_PIPE_WRITE:
        if (ofd->pipe->readers == 0)
            mask |= POSIX_POLL_ERR;
        else if (ofd->pipe->length < ofd->pipe->capacity)
            mask |= POSIX_POLL_OUT;
        break;
    case POSIX_OFD_REGULAR:
        if (ofd->regular.readable &&
            ofd->regular.offset < ofd->regular.file->data_capacity)
            mask |= POSIX_POLL_IN;
        if (ofd->regular.writable) mask |= POSIX_POLL_OUT;
        break;
    case POSIX_OFD_DIRECTORY:
        mask |= POSIX_POLL_IN;
        break;
    }
    return mask;
}

/* --- Terminal operations --- */

int posix_kernel_terminal_enqueue(posix_kernel *kernel, int fd,
                                  const uint8_t *data, int length) {
    if (!kernel || !fd_valid(fd)) return -POSIX_EINVAL;
    if (!data || length < 0) return -POSIX_EINVAL;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;
    if (ofd->kind != POSIX_OFD_TERMINAL) return -POSIX_EINVAL;

    int avail = ofd->terminal.input_capacity - ofd->terminal.input_length;
    for (int i = 0; i < length && avail > 0; i++) {
        uint8_t byte = data[i];
        if ((ofd->terminal.termios.lflag & POSIX_TERMIOS_LFLAG_ISIG) &&
            byte == ofd->terminal.termios.cc[POSIX_TERMIOS_VINTR]) {
            ofd->terminal.input_length = 0;
            if (ofd->terminal.foreground_pgid == kernel->process_group_id)
                posix_kernel_signal_raise(kernel, 2);
            else
                signal_bit_set(&ofd->terminal.pending_signals, 2);
            avail = ofd->terminal.input_capacity;
            continue;
        }
        if (ofd->terminal.termios.lflag & POSIX_TERMIOS_LFLAG_ICANON) {
            if (byte == '\r' && (ofd->terminal.termios.iflag & POSIX_TERMIOS_IFLAG_ICRNL))
                byte = '\n';
            if (byte == ofd->terminal.termios.cc[POSIX_TERMIOS_VERASE] || byte == 8) {
                if (ofd->terminal.input_length > 0 &&
                    ofd->terminal.input[ofd->terminal.input_length - 1] != '\n')
                    ofd->terminal.input_length--;
                avail = ofd->terminal.input_capacity - ofd->terminal.input_length;
                continue;
            }
            if (byte == ofd->terminal.termios.cc[POSIX_TERMIOS_VKILL]) {
                while (ofd->terminal.input_length > 0 &&
                       ofd->terminal.input[ofd->terminal.input_length - 1] != '\n')
                    ofd->terminal.input_length--;
                avail = ofd->terminal.input_capacity - ofd->terminal.input_length;
                continue;
            }
            if (byte == ofd->terminal.termios.cc[POSIX_TERMIOS_VEOF]) {
                /* VEOF terminates pending canonical input without adding a
                 * byte, and produces immediate EOF on an empty line. */
                ofd->terminal.eof = 1;
                continue;
            }
        }
        ofd->terminal.input[ofd->terminal.input_length++] = byte;
        avail--;
    }
    return 0;
}

int posix_kernel_terminal_signal_eof(posix_kernel *kernel, int fd) {
    if (!kernel || !fd_valid(fd)) return -POSIX_EINVAL;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;
    if (ofd->kind != POSIX_OFD_TERMINAL) return -POSIX_EINVAL;
    ofd->terminal.eof = 1;
    return 0;
}

int posix_kernel_terminal_process_output(const posix_kernel *kernel, int fd,
                                         const uint8_t *input, int length,
                                         uint8_t *output, int capacity) {
    if (!kernel || !fd_valid(fd) || !input || length < 0 || !output || capacity < 0)
        return -POSIX_EINVAL;
    const posix_ofd *ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;
    if (ofd->kind != POSIX_OFD_TERMINAL) return -POSIX_EINVAL;
    int post = (ofd->terminal.termios.oflag & POSIX_TERMIOS_OFLAG_OPOST) != 0;
    int onlcr = (ofd->terminal.termios.oflag & POSIX_TERMIOS_OFLAG_ONLCR) != 0;
    if (!post || !onlcr) {
        if (length > capacity) return -POSIX_E2BIG;
        if (length > 0) memcpy(output, input, (size_t)length);
        return length;
    }
    int written = 0;
    for (int i = 0; i < length; i++) {
        int extra = input[i] == '\n' ? 2 : 1;
        if (written > capacity - extra) return -POSIX_E2BIG;
        if (extra == 2) output[written++] = '\r';
        output[written++] = input[i];
    }
    return written;
}

int posix_kernel_isatty(const posix_kernel *kernel, int fd) {
    return kernel && fd_valid(fd) && kernel->fds[fd].ofd &&
           kernel->fds[fd].ofd->kind == POSIX_OFD_TERMINAL;
}

int posix_kernel_tcgetattr(posix_kernel *kernel, int fd,
                           posix_termios *termios) {
    if (!termios) return -POSIX_EFAULT;
    if (!posix_kernel_isatty(kernel, fd)) return -POSIX_EBADF;
    *termios = kernel->fds[fd].ofd->terminal.termios;
    return 0;
}

int posix_kernel_tcsetattr(posix_kernel *kernel, int fd,
                           const posix_termios *termios) {
    if (!termios) return -POSIX_EFAULT;
    if (!posix_kernel_isatty(kernel, fd)) return -POSIX_EBADF;
    kernel->fds[fd].ofd->terminal.termios = *termios;
    return 0;
}

int posix_kernel_tcflow(posix_kernel *kernel, int fd, int action) {
    if (!posix_kernel_isatty(kernel, fd)) return -POSIX_EBADF;
    /* The browser terminal has no hardware flow-control queue. Validate the
     * action while keeping its input/output streams continuously enabled. */
    if (action < POSIX_TCOOFF || action > POSIX_TCION)
        return -POSIX_EINVAL;
    return 0;
}

int posix_kernel_terminal_get_winsize(const posix_kernel *kernel, int fd,
                                      posix_winsize *winsize) {
    if (!winsize) return -POSIX_EFAULT;
    if (!posix_kernel_isatty(kernel, fd)) return -POSIX_EBADF;
    *winsize = kernel->fds[fd].ofd->terminal.winsize;
    return 0;
}

int posix_kernel_terminal_set_winsize(posix_kernel *kernel, int fd,
                                      const posix_winsize *winsize) {
    if (!winsize) return -POSIX_EFAULT;
    if (!posix_kernel_isatty(kernel, fd)) return -POSIX_EBADF;
    kernel->fds[fd].ofd->terminal.winsize = *winsize;
    (void)posix_kernel_signal_raise(kernel, POSIX_SIGWINCH);
    return 0;
}

/* --- Pipe --- */

int posix_kernel_pipe(posix_kernel *kernel, int fds[2]) {
    if (!kernel || !fds) return -POSIX_EINVAL;

    int rfd = kernel_find_free_fd(kernel, 0);
    if (rfd < 0) return rfd;
    int wfd = kernel_find_free_fd(kernel, rfd + 1);
    if (wfd < 0) return wfd;

    posix_pipe *p = calloc(1, sizeof(posix_pipe));
    if (!p) return -POSIX_ENOMEM;
    p->buffer = malloc(POSIX_PIPE_CAPACITY);
    if (!p->buffer) { free(p); return -POSIX_ENOMEM; }
    p->length = 0;
    p->capacity = POSIX_PIPE_CAPACITY;
    p->readers = 1;
    p->writers = 1;

    posix_ofd *rofd = ofd_alloc(POSIX_OFD_PIPE_READ);
    if (!rofd) { free(p->buffer); free(p); return -POSIX_ENOMEM; }
    rofd->pipe = p;

    posix_ofd *wofd = ofd_alloc(POSIX_OFD_PIPE_WRITE);
    if (!wofd) { ofd_release(rofd); return -POSIX_ENOMEM; }
    wofd->pipe = p;

    kernel->fds[rfd].ofd = rofd;
    kernel->fds[wfd].ofd = wofd;
    fds[0] = rfd;
    fds[1] = wfd;
    return 0;
}

/* --- Descriptor operations --- */

int posix_kernel_close(posix_kernel *kernel, int fd) {
    if (!kernel || !fd_valid(fd)) return -POSIX_EINVAL;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;
    kernel->fds[fd].ofd = NULL;
    pipe_count_dec(ofd);
    ofd_release(ofd);
    return 0;
}

int posix_kernel_open(posix_kernel *kernel, const uint8_t *path, size_t length,
                      int flags, int mode) {
    if (!kernel || !path || length == 0) return -POSIX_EINVAL;
    if (flags & ~(POSIX_O_WRONLY | POSIX_O_RDWR | POSIX_O_CREAT | POSIX_O_EXCL |
                  POSIX_O_TRUNC | POSIX_O_APPEND)) return -POSIX_EINVAL;
    if ((flags & POSIX_O_WRONLY) && (flags & POSIX_O_RDWR))
        return -POSIX_EINVAL;
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    int result = path_normalize(kernel, path, length, normalized);
    if (result < 0) return result;
    posix_kernel_path_node *node = path_find(kernel, normalized);
    if (!node) {
        if (!(flags & POSIX_O_CREAT)) return path_prefix_is_file(kernel, normalized) ?
            -POSIX_ENOTDIR : -POSIX_ENOENT;
        posix_path_metadata metadata = { POSIX_NODE_REGULAR,
            (uint32_t)(mode ? mode : 0666), 0, 0, 0,
            1000u + (uint64_t)kernel->path_node_count };
        result = posix_kernel_path_add_data(kernel, normalized, &metadata, NULL, 0);
        if (result < 0) return result;
        node = path_find(kernel, normalized);
    }
    if (!node) return -POSIX_ENOENT;
    if (node->metadata.kind == POSIX_NODE_DIRECTORY) {
        if (flags & (POSIX_O_WRONLY | POSIX_O_RDWR | POSIX_O_CREAT |
                     POSIX_O_TRUNC | POSIX_O_APPEND)) return -POSIX_EISDIR;
        int directory_fd = kernel_find_free_fd(kernel, 0);
        if (directory_fd < 0) return directory_fd;
        posix_ofd *directory = ofd_alloc(POSIX_OFD_DIRECTORY);
        if (!directory) return -POSIX_ENOMEM;
        directory->directory.node = node;
        directory->directory.index = 0;
        kernel->fds[directory_fd].ofd = directory;
        return directory_fd;
    }
    if (node->metadata.kind != POSIX_NODE_REGULAR) return -POSIX_ENOENT;
    int fd = kernel_find_free_fd(kernel, 0);
    if (fd < 0) return fd;
    posix_ofd *ofd = ofd_alloc(POSIX_OFD_REGULAR);
    if (!ofd) return -POSIX_ENOMEM;
    ofd->regular.node = node;
    ofd->regular.file = node->file;
    posix_file_object_retain(ofd->regular.file);
    ofd->regular.offset = (flags & POSIX_O_APPEND) ?
        ofd->regular.file->data_capacity : 0;
    ofd->regular.readable = !(flags & POSIX_O_WRONLY);
    ofd->regular.writable = (flags & (POSIX_O_WRONLY | POSIX_O_RDWR)) != 0;
    ofd->regular.append = (flags & POSIX_O_APPEND) != 0;
    if ((flags & POSIX_O_TRUNC) && ofd->regular.writable) {
        free(ofd->regular.file->data); ofd->regular.file->data = NULL;
        ofd->regular.file->data_capacity = 0;
        node->metadata.size = 0;
    }
    kernel->fds[fd].ofd = ofd;
    kernel->fds[fd].cloexec = 0;
    return fd;
}

static int shm_internal_name(const uint8_t *name, size_t length,
                             char *path, size_t capacity) {
    static const char prefix[] = "/__waste_shm__";
    size_t prefix_length = sizeof(prefix) - 1;
    if (!name || !path || length < 2 || name[0] != '/' ||
        length >= POSIX_PATH_MAX || prefix_length + length >= capacity)
        return -POSIX_EINVAL;
    for (size_t i = 1; i < length; i++)
        if (name[i] == '/') return -POSIX_EINVAL;
    memcpy(path, prefix, prefix_length);
    memcpy(path + prefix_length, name, length);
    path[prefix_length + length] = 0;
    return 0;
}

int posix_kernel_shm_open(posix_kernel *kernel, const uint8_t *name,
                          size_t length, int flags, int mode) {
    posix_shm_object *object = NULL;
    posix_kernel_path_node *node;
    char path[POSIX_PATH_NODE_NAME_MAX];
    int created = 0;
    int status = shm_internal_name(name, length, path, sizeof(path));
    if (status < 0) return status;
    if (!kernel->shm_namespace) return -POSIX_ENOSYS;
    for (uint32_t i = 0; i < kernel->shm_namespace->count; i++)
        if (strlen(kernel->shm_namespace->objects[i].name) == length &&
            memcmp(kernel->shm_namespace->objects[i].name, name, length) == 0) {
            if ((flags & (POSIX_O_CREAT | POSIX_O_EXCL)) ==
                (POSIX_O_CREAT | POSIX_O_EXCL))
                return -POSIX_EEXIST;
            object = &kernel->shm_namespace->objects[i];
            break;
        }
    if (!object) {
        if (!(flags & POSIX_O_CREAT)) return -POSIX_ENOENT;
        if (flags & POSIX_O_EXCL) return -POSIX_EEXIST;
        if (kernel->shm_namespace->count >= POSIX_SHM_OBJECT_MAX)
            return -POSIX_ENOMEM;
        object = &kernel->shm_namespace->objects[kernel->shm_namespace->count++];
        memcpy(object->name, name, length);
        object->name[length] = 0;
        object->file = file_object_create(NULL, 0,
            UINT64_C(0x40000000) + kernel->shm_namespace->count);
        if (!object->file) {
            kernel->shm_namespace->count--;
            return -POSIX_ENOMEM;
        }
        object->mode = (uint32_t)(mode ? mode : 0666) & 0777u;
        object->uid = kernel->uid;
        object->gid = kernel->gid;
        created = 1;
    }
    if (kernel->uid != 0) {
        uint32_t permissions = object->uid == kernel->uid ?
            (object->mode >> 6) & 7u :
            (object->gid == kernel->gid ? (object->mode >> 3) & 7u :
                                          object->mode & 7u);
        uint32_t required = (flags & (POSIX_O_WRONLY | POSIX_O_RDWR)) ? 2u : 4u;
        if ((permissions & required) != required) {
            if (created) {
                posix_kernel_file_release(object->file);
                memset(object, 0, sizeof(*object));
                kernel->shm_namespace->count--;
            }
            return -POSIX_EACCES;
        }
    }
    node = path_find(kernel, path);
    if (!node) {
        posix_path_metadata metadata = { POSIX_NODE_REGULAR, 0666, 0, 0, 0,
            object->file->inode };
        status = posix_kernel_path_add_data(kernel, path, &metadata, NULL, 0);
        if (status < 0) return status;
        node = path_find(kernel, path);
    }
    if (!node) return -POSIX_ENOENT;
    if (node->file != object->file) {
        posix_file_object_retain(object->file);
        posix_kernel_file_release(node->file);
        node->file = object->file;
    }
    node->metadata.size = (int64_t)object->file->data_capacity;
    return posix_kernel_open(kernel, (const uint8_t *)path, strlen(path),
                             flags, mode);
}

int posix_kernel_shm_unlink(posix_kernel *kernel, const uint8_t *name,
                            size_t length) {
    char path[POSIX_PATH_NODE_NAME_MAX];
    int status = shm_internal_name(name, length, path, sizeof(path));
    if (status < 0) return status;
    if (!kernel->shm_namespace) return -POSIX_ENOSYS;
    for (uint32_t i = 0; i < kernel->shm_namespace->count; i++)
        if (strlen(kernel->shm_namespace->objects[i].name) == length &&
            memcmp(kernel->shm_namespace->objects[i].name, name, length) == 0) {
            posix_kernel_file_release(kernel->shm_namespace->objects[i].file);
            for (; i + 1 < kernel->shm_namespace->count; i++)
                kernel->shm_namespace->objects[i] =
                    kernel->shm_namespace->objects[i + 1];
            memset(&kernel->shm_namespace->objects[
                       --kernel->shm_namespace->count], 0,
                   sizeof(kernel->shm_namespace->objects[0]));
            return 0;
        }
    (void)path;
    return -POSIX_ENOENT;
}

int posix_kernel_readdir(posix_kernel *kernel, int fd, char *name,
                         size_t capacity, posix_path_metadata *metadata) {
    if (!kernel || !fd_valid(fd) || !name || capacity == 0 || !metadata)
        return -POSIX_EINVAL;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;
    if (ofd->kind != POSIX_OFD_DIRECTORY) return -POSIX_ENOTDIR;
    const char *parent = ofd->directory.node->path;
    size_t parent_length = strlen(parent);
    for (int i = ofd->directory.index; i < kernel->path_node_count; i++) {
        const char *candidate = kernel->path_nodes[i].path;
        size_t length = strlen(candidate);
        size_t start = parent_length == 1 ? 1 : parent_length + 1;
        if (length <= start || strncmp(candidate, parent, parent_length) != 0 ||
            (parent_length > 1 && candidate[parent_length] != '/')) continue;
        int nested = 0;
        for (size_t at = start; at < length; at++)
            if (candidate[at] == '/') { nested = 1; break; }
        if (nested) continue;
        size_t name_length = length - start;
        ofd->directory.index = i + 1;
        if (name_length + 1 > capacity) return -POSIX_ERANGE;
        memcpy(name, candidate + start, name_length + 1);
        *metadata = kernel->path_nodes[i].metadata;
        return 1;
    }
    ofd->directory.index = kernel->path_node_count;
    return 0;
}

int posix_kernel_lseek(posix_kernel *kernel, int fd, int64_t offset,
                       int whence, int64_t *result) {
    if (!kernel || !result || !fd_valid(fd)) return -POSIX_EINVAL;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (!ofd || ofd->kind != POSIX_OFD_REGULAR) return -POSIX_ESPIPE;
    int64_t base = whence == 0 ? 0 : whence == 1 ? (int64_t)ofd->regular.offset :
        whence == 2 ? (int64_t)ofd->regular.file->data_capacity : INT64_MIN;
    if (base == INT64_MIN || offset < -base) return -POSIX_EINVAL;
    ofd->regular.offset = (size_t)(base + offset);
    *result = (int64_t)ofd->regular.offset;
    return 0;
}

int posix_kernel_dup(posix_kernel *kernel, int oldfd) {
    if (!kernel || !fd_valid(oldfd)) return -POSIX_EINVAL;
    posix_ofd *ofd = kernel->fds[oldfd].ofd;
    if (!ofd) return -POSIX_EBADF;

    int newfd = kernel_find_free_fd(kernel, 0);
    if (newfd < 0) return newfd;

    ofd->ref_count++;
    if (ofd->kind == POSIX_OFD_PIPE_READ) ofd->pipe->readers++;
    if (ofd->kind == POSIX_OFD_PIPE_WRITE) ofd->pipe->writers++;
    kernel->fds[newfd].ofd = ofd;
    kernel->fds[newfd].cloexec = 0;
    return newfd;
}

int posix_kernel_dupfd(posix_kernel *kernel, int oldfd, int minfd,
                       int cloexec) {
    if (!kernel || !fd_valid(oldfd) || minfd < 0) return -POSIX_EINVAL;
    posix_ofd *ofd = kernel->fds[oldfd].ofd;
    if (!ofd) return -POSIX_EBADF;

    int newfd = kernel_find_free_fd(kernel, minfd);
    if (newfd < 0) return newfd;

    ofd->ref_count++;
    if (ofd->kind == POSIX_OFD_PIPE_READ) ofd->pipe->readers++;
    if (ofd->kind == POSIX_OFD_PIPE_WRITE) ofd->pipe->writers++;
    kernel->fds[newfd].ofd = ofd;
    kernel->fds[newfd].cloexec = cloexec ? 1 : 0;
    return newfd;
}

int posix_kernel_fchdir(posix_kernel *kernel, int fd) {
    if (!kernel || !fd_valid(fd)) return -POSIX_EINVAL;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;
    if (ofd->kind != POSIX_OFD_DIRECTORY) return -POSIX_ENOTDIR;
    return posix_kernel_path_set_cwd(kernel, ofd->directory.node->path);
}

int posix_kernel_dup2(posix_kernel *kernel, int oldfd, int newfd) {
    if (!kernel || !fd_valid(oldfd) || !fd_valid(newfd)) return -POSIX_EINVAL;
    posix_ofd *ofd = kernel->fds[oldfd].ofd;
    if (!ofd) return -POSIX_EBADF;
    if (oldfd == newfd) return newfd;

    /* Close newfd if open */
    if (kernel->fds[newfd].ofd)
        posix_kernel_close(kernel, newfd);

    ofd->ref_count++;
    if (ofd->kind == POSIX_OFD_PIPE_READ) ofd->pipe->readers++;
    if (ofd->kind == POSIX_OFD_PIPE_WRITE) ofd->pipe->writers++;
    kernel->fds[newfd].ofd = ofd;
    kernel->fds[newfd].cloexec = 0;
    return newfd;
}

int posix_kernel_get_cloexec(const posix_kernel *kernel, int fd) {
    if (!kernel || !fd_valid(fd) || !kernel->fds[fd].ofd) return -POSIX_EBADF;
    return kernel->fds[fd].cloexec ? POSIX_FD_CLOEXEC : 0;
}

int posix_kernel_set_cloexec(posix_kernel *kernel, int fd, int enabled) {
    if (!kernel || !fd_valid(fd) || !kernel->fds[fd].ofd)
        return -POSIX_EBADF;
    if (enabled != 0 && enabled != POSIX_FD_CLOEXEC) return -POSIX_EINVAL;
    kernel->fds[fd].cloexec = enabled ? 1 : 0;
    return 0;
}

void posix_kernel_close_on_exec(posix_kernel *kernel) {
    if (!kernel) return;
    for (int fd = 0; fd < POSIX_KERNEL_FD_MAX; fd++)
        if (kernel->fds[fd].ofd && kernel->fds[fd].cloexec)
            (void)posix_kernel_close(kernel, fd);
}

/* --- I/O --- */

int posix_kernel_read(posix_kernel *kernel, int fd, void *buf, int count) {
    if (!kernel || !fd_valid(fd)) return -POSIX_EINVAL;
    if (!buf || count < 0) return -POSIX_EINVAL;
    if (count == 0) return 0;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;

    switch (ofd->kind) {
    case POSIX_OFD_TERMINAL: {
        if (ofd->terminal.input_length == 0) {
            if (ofd->terminal.eof) return 0;
            if (!(ofd->terminal.termios.lflag & POSIX_TERMIOS_LFLAG_ICANON) &&
                ofd->terminal.termios.cc[POSIX_TERMIOS_VMIN] == 0 &&
                ofd->terminal.termios.cc[POSIX_TERMIOS_VTIME] == 0)
                return 0;
            terminal_wait_with_timeout(kernel, fd,
                ofd->terminal.termios.cc[POSIX_TERMIOS_VTIME]);
            if (!(ofd->terminal.termios.lflag & POSIX_TERMIOS_LFLAG_ICANON) &&
                kernel->wait.has_deadline && kernel->clock_now &&
                kernel->clock_now(kernel->clock_data) >= kernel->wait.deadline_ns) {
                posix_kernel_cancel_wait(kernel);
                return 0;
            }
            return -POSIX_EAGAIN;
        }
        int available = ofd->terminal.input_length;
        if (ofd->terminal.termios.lflag & POSIX_TERMIOS_LFLAG_ICANON) {
            available = 0;
            while (available < ofd->terminal.input_length &&
                   ofd->terminal.input[available] != '\n') available++;
            if (available < ofd->terminal.input_length) available++;
            else if (!ofd->terminal.eof) {
                posix_kernel_wait_read(kernel, fd);
                return -POSIX_EAGAIN;
            }
        } else {
            uint8_t vmin = ofd->terminal.termios.cc[POSIX_TERMIOS_VMIN];
            uint8_t vtime = ofd->terminal.termios.cc[POSIX_TERMIOS_VTIME];
            if (vmin > 0 && available < vmin && !ofd->terminal.eof) {
                terminal_wait_with_timeout(kernel, fd, vtime);
                if (kernel->wait.has_deadline && kernel->clock_now &&
                    kernel->clock_now(kernel->clock_data) >= kernel->wait.deadline_ns) {
                    posix_kernel_cancel_wait(kernel);
                    /* A VMIN/VTIME read returns bytes received before the
                     * timer expired, even when fewer than VMIN arrived. */
                } else {
                    return -POSIX_EAGAIN;
                }
            }
        }
        int n = count < available ? count : available;
        memcpy(buf, ofd->terminal.input, n);
        ofd->terminal.input_length -= n;
        if (ofd->terminal.input_length > 0)
            memmove(ofd->terminal.input, ofd->terminal.input + n,
                    ofd->terminal.input_length);
        posix_kernel_cancel_wait(kernel);
        return n;
    }
    case POSIX_OFD_PIPE_READ: {
        posix_pipe *p = ofd->pipe;
        if (p->length == 0) {
            if (p->writers == 0) return 0;
            posix_kernel_wait_read(kernel, fd);
            return -POSIX_EAGAIN;
        }
        int n = count < p->length ? count : p->length;
        memcpy(buf, p->buffer, n);
        p->length -= n;
        if (p->length > 0)
            memmove(p->buffer, p->buffer + n, p->length);
        posix_kernel_cancel_wait(kernel);
        return n;
    }
    case POSIX_OFD_REGULAR: {
        if (!ofd->regular.readable) return -POSIX_EBADF;
        if (ofd->regular.offset >= ofd->regular.file->data_capacity) return 0;
        size_t available = ofd->regular.file->data_capacity - ofd->regular.offset;
        int n = available < (size_t)count ? (int)available : count;
        memcpy(buf, ofd->regular.file->data + ofd->regular.offset, (size_t)n);
        ofd->regular.offset += (size_t)n;
        return n;
    }
    case POSIX_OFD_PIPE_WRITE:
        return -POSIX_EBADF;
    case POSIX_OFD_DIRECTORY:
        return -POSIX_EISDIR;
    }
    return -POSIX_EINVAL;
}

int posix_kernel_file_read_at(posix_kernel *kernel, int fd, uint64_t offset,
                              void *buf, size_t count) {
    posix_ofd *ofd;
    if (!kernel || !fd_valid(fd) || (!buf && count != 0))
        return -POSIX_EINVAL;
    ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;
    if (ofd->kind != POSIX_OFD_REGULAR || !ofd->regular.readable)
        return -POSIX_EBADF;
    if (count > INT_MAX) return -POSIX_EINVAL;
    if (offset > ofd->regular.file->data_capacity ||
        count > ofd->regular.file->data_capacity - offset)
        return -POSIX_EINVAL;
    if (count) memcpy(buf, ofd->regular.file->data + (size_t)offset, count);
    return (int)count;
}

int posix_kernel_file_size(posix_kernel *kernel, int fd, uint64_t *size_out) {
    posix_ofd *ofd;
    if (!kernel || !fd_valid(fd) || !size_out) return -POSIX_EINVAL;
    ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;
    if (ofd->kind != POSIX_OFD_REGULAR || !ofd->regular.readable)
        return -POSIX_EBADF;
    *size_out = ofd->regular.file->data_capacity;
    return 0;
}

int posix_kernel_file_identity(posix_kernel *kernel, int fd,
                               uint64_t *object_id_out,
                               int *writable_out) {
    posix_ofd *ofd;
    if (!kernel || !fd_valid(fd) || !object_id_out || !writable_out)
        return -POSIX_EINVAL;
    ofd = kernel->fds[fd].ofd;
    if (!ofd || ofd->kind != POSIX_OFD_REGULAR || !ofd->regular.readable)
        return -POSIX_EBADF;
    *object_id_out = ofd->regular.file->inode;
    *writable_out = ofd->regular.writable != 0;
    return 0;
}

int posix_kernel_ftruncate(posix_kernel *kernel, int fd, uint64_t size) {
    posix_ofd *ofd;
    uint8_t *resized;
    if (!kernel || !fd_valid(fd) || size > SIZE_MAX)
        return -POSIX_EINVAL;
    ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;
    if (ofd->kind != POSIX_OFD_REGULAR || !ofd->regular.writable)
        return -POSIX_EBADF;
    if ((size_t)size == ofd->regular.file->data_capacity) return 0;
    resized = (uint8_t *)realloc(ofd->regular.file->data, (size_t)size);
    if (!resized && size != 0) return -POSIX_ENOMEM;
    if ((size_t)size > ofd->regular.file->data_capacity)
        memset(resized + ofd->regular.file->data_capacity, 0,
               (size_t)size - ofd->regular.file->data_capacity);
    ofd->regular.file->data = resized;
    ofd->regular.file->data_capacity = (size_t)size;
    return 0;
}

int posix_kernel_file_retain(posix_kernel *kernel, int fd,
                             posix_file_object **object_out,
                             int *writable_out) {
    posix_ofd *ofd;
    if (!kernel || !fd_valid(fd) || !object_out || !writable_out)
        return -POSIX_EINVAL;
    ofd = kernel->fds[fd].ofd;
    if (!ofd || ofd->kind != POSIX_OFD_REGULAR || !ofd->regular.readable)
        return -POSIX_EBADF;
    posix_file_object_retain(ofd->regular.file);
    *object_out = ofd->regular.file;
    *writable_out = ofd->regular.writable != 0;
    return 0;
}

int posix_kernel_file_write_object(posix_kernel *kernel,
                                   posix_file_object *object,
                                   uint64_t offset, const void *buf,
                                   size_t count) {
    uint64_t end;
    if (!kernel || !object || (!buf && count != 0) || count > INT_MAX)
        return -POSIX_EINVAL;
    if (offset > UINT64_MAX - count) return -POSIX_EINVAL;
    end = offset + count;
    if (end > SIZE_MAX) return -POSIX_EINVAL;
    if (end > object->data_capacity) {
        uint8_t *grown = realloc(object->data, (size_t)end);
        if (!grown) return -POSIX_ENOMEM;
        if ((size_t)end > object->data_capacity)
            memset(grown + object->data_capacity, 0,
                   (size_t)end - object->data_capacity);
        object->data = grown;
        object->data_capacity = (size_t)end;
    }
    if (count) memcpy(object->data + (size_t)offset, buf, count);
    return (int)count;
}

int posix_kernel_file_write_at(posix_kernel *kernel, int fd, uint64_t offset,
                               const void *buf, size_t count) {
    posix_ofd *ofd;
    if (!kernel || !fd_valid(fd) || (!buf && count != 0) || count > INT_MAX)
        return -POSIX_EINVAL;
    ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;
    if (ofd->kind != POSIX_OFD_REGULAR || !ofd->regular.writable)
        return -POSIX_EBADF;
    return posix_kernel_file_write_object(
        kernel, ofd->regular.file, offset, buf, count);
}

int posix_kernel_write(posix_kernel *kernel, int fd,
                       const void *buf, int count) {
    if (!kernel || !fd_valid(fd)) return -POSIX_EINVAL;
    if (!buf || count < 0) return -POSIX_EINVAL;
    if (count == 0) return 0;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;

    switch (ofd->kind) {
    case POSIX_OFD_TERMINAL:
        /* Terminal output always succeeds (browser consumes it). */
        return count;
    case POSIX_OFD_PIPE_WRITE: {
        posix_pipe *p = ofd->pipe;
        if (p->readers == 0) return -POSIX_EPIPE;
        int avail = p->capacity - p->length;
        if (avail == 0) return -POSIX_EAGAIN;
        int n = count < avail ? count : avail;
        memcpy(p->buffer + p->length, buf, n);
        p->length += n;
        posix_kernel_cancel_wait(kernel);
        return n;
    }
    case POSIX_OFD_PIPE_READ:
        return -POSIX_EBADF;
    case POSIX_OFD_DIRECTORY:
        return -POSIX_EISDIR;
    case POSIX_OFD_REGULAR: {
        if (!ofd->regular.writable) return -POSIX_EBADF;
        size_t offset = ofd->regular.append ? ofd->regular.file->data_capacity :
            ofd->regular.offset;
        size_t needed = offset + (size_t)count;
        if (needed < offset) return -POSIX_ENOSPC;
        if (needed > ofd->regular.file->data_capacity) {
            uint8_t *grown = realloc(ofd->regular.file->data, needed);
            if (!grown) return -POSIX_ENOMEM;
            ofd->regular.file->data = grown;
            ofd->regular.file->data_capacity = needed;
        }
        memcpy(ofd->regular.file->data + offset, buf, (size_t)count);
        ofd->regular.offset = offset + (size_t)count;
        return count;
    }
    }
    return -POSIX_EINVAL;
}

/* --- Select/pselect --- */

static int kernel_select_core(posix_kernel *kernel, int nfds,
                              posix_fd_set *readfds, posix_fd_set *writefds,
                              posix_fd_set *exceptfds, int is_zero_timeout,
                              int has_deadline, uint64_t deadline_ns,
                              const posix_sigset *temporary_mask) {
    if (!kernel) return -POSIX_EINVAL;
    route_terminal_signals(kernel);
    if (posix_nfds_validate(nfds) != 0) {
        posix_kernel_cancel_wait(kernel);
        return -POSIX_EINVAL;
    }
    int had_wait = kernel->wait.active;

    /* pselect installs its mask before the readiness/pending-signal check. */
    if (!had_wait && temporary_mask) {
        kernel->wait.saved_mask = kernel->signal_mask;
        kernel->wait.temporary_mask = *temporary_mask;
        kernel->wait.has_signal_mask = 1;
        kernel->signal_mask = *temporary_mask;
    }

    /* Fds beyond the kernel table cannot be open. */
    int scan_max = nfds < POSIX_KERNEL_FD_MAX ? nfds : POSIX_KERNEL_FD_MAX;
    for (int fd = scan_max; fd < nfds; fd++) {
        if ((readfds && posix_fd_isset(fd, readfds)) ||
            (writefds && posix_fd_isset(fd, writefds)) ||
            (exceptfds && posix_fd_isset(fd, exceptfds))) {
            posix_kernel_cancel_wait(kernel);
            return -POSIX_EBADF;
        }
    }

    if (kernel->wait.active) {
        int wait_status = posix_kernel_wait_poll(kernel);
        if (wait_status == POSIX_WAIT_SIGNAL) {
            int signal = first_unmasked_pending(kernel);
            if (signal) {
                signal_bit_clear(&kernel->pending_signals, signal);
                kernel->delivered_signal = signal;
            }
            posix_kernel_cancel_wait(kernel);
            if (readfds) posix_fd_zero(readfds);
            if (writefds) posix_fd_zero(writefds);
            if (exceptfds) posix_fd_zero(exceptfds);
            return -POSIX_EINTR;
        }
        if (wait_status == POSIX_WAIT_TIMEOUT) {
            posix_kernel_cancel_wait(kernel);
            if (readfds) posix_fd_zero(readfds);
            if (writefds) posix_fd_zero(writefds);
            if (exceptfds) posix_fd_zero(exceptfds);
            return 0;
        }
        /* A readiness transition is checked again below so output sets are
           built from the current guest interests, not the copied record. */
    }

    if (first_unmasked_pending(kernel)) {
        int signal = first_unmasked_pending(kernel);
        signal_bit_clear(&kernel->pending_signals, signal);
        kernel->delivered_signal = signal;
        posix_kernel_cancel_wait(kernel);
        if (readfds) posix_fd_zero(readfds);
        if (writefds) posix_fd_zero(writefds);
        if (exceptfds) posix_fd_zero(exceptfds);
        return -POSIX_EINTR;
    }

    posix_fd_set out_read, out_write, out_except;
    posix_fd_zero(&out_read);
    posix_fd_zero(&out_write);
    posix_fd_zero(&out_except);
    int count = 0;

    for (int fd = 0; fd < scan_max; fd++) {
        int want_read = readfds && posix_fd_isset(fd, readfds);
        int want_write = writefds && posix_fd_isset(fd, writefds);
        int want_except = exceptfds && posix_fd_isset(fd, exceptfds);
        if (!want_read && !want_write && !want_except) continue;

        int readiness = posix_kernel_query_readiness(kernel, fd);
        if (readiness < 0) {
            posix_kernel_cancel_wait(kernel);
            return -POSIX_EBADF;
        }

        if (want_read && (readiness & (POSIX_POLL_IN | POSIX_POLL_HUP))) {
            posix_fd_set_bit(fd, &out_read);
            count++;
        }
        if (want_write && (readiness & POSIX_POLL_OUT)) {
            posix_fd_set_bit(fd, &out_write);
            count++;
        }
        if (want_except && (readiness & POSIX_POLL_ERR)) {
            posix_fd_set_bit(fd, &out_except);
            count++;
        }
    }

    if (count > 0) {
        posix_kernel_cancel_wait(kernel);
        if (readfds) *readfds = out_read;
        if (writefds) *writefds = out_write;
        if (exceptfds) *exceptfds = out_except;
        return count;
    }

    /* Nothing ready. */
    if (is_zero_timeout) {
        posix_kernel_cancel_wait(kernel);
        if (readfds) posix_fd_zero(readfds);
        if (writefds) posix_fd_zero(writefds);
        if (exceptfds) posix_fd_zero(exceptfds);
        return 0;
    }

    /* Preserve only copied interests and a deadline.  The executor turns this
       result into EXEC_YIELD; a later invocation polls the same record. */
    if (kernel->wait.active) {
        has_deadline = kernel->wait.has_deadline;
        deadline_ns = kernel->wait.deadline_ns;
    }
    kernel->wait.active = 1;
    if (!had_wait) {
        kernel->wait.generation++;
        if (kernel->wait.generation == 0) kernel->wait.generation = 1;
    }
    kernel->wait.nfds = nfds;
    if (readfds) kernel->wait.readfds = *readfds;
    else posix_fd_zero(&kernel->wait.readfds);
    if (writefds) kernel->wait.writefds = *writefds;
    else posix_fd_zero(&kernel->wait.writefds);
    if (exceptfds) kernel->wait.exceptfds = *exceptfds;
    else posix_fd_zero(&kernel->wait.exceptfds);
    kernel->wait.has_deadline = has_deadline;
    kernel->wait.deadline_ns = deadline_ns;
    if (!kernel->wait.has_signal_mask && temporary_mask) {
        kernel->wait.saved_mask = kernel->signal_mask;
        kernel->wait.temporary_mask = *temporary_mask;
        kernel->wait.has_signal_mask = 1;
        kernel->signal_mask = *temporary_mask;
    }
    return -POSIX_EAGAIN;
}

static uint64_t timeout_ns(int64_t seconds, int32_t fraction,
                           uint64_t fraction_scale) {
    uint64_t scale = UINT64_C(1000000000);
    uint64_t sec = (uint64_t)seconds;
    if (sec > UINT64_MAX / scale) return UINT64_MAX;
    uint64_t result = sec * scale;
    uint64_t part = (uint64_t)fraction * fraction_scale;
    if (UINT64_MAX - result < part) return UINT64_MAX;
    return result + part;
}

static uint64_t deadline_from(posix_kernel *kernel, uint64_t duration) {
    if (!kernel->clock_now) return 0;
    uint64_t now = kernel->clock_now(kernel->clock_data);
    if (UINT64_MAX - now < duration) return UINT64_MAX;
    return now + duration;
}

int posix_kernel_select(posix_kernel *kernel, int nfds,
                        posix_fd_set *readfds, posix_fd_set *writefds,
                        posix_fd_set *exceptfds, const posix_timeval *timeout) {
    int is_zero = 0, has_deadline = 0;
    uint64_t deadline = 0;
    if (timeout) {
        if (posix_timeval_validate(timeout) != 0) {
            posix_kernel_cancel_wait(kernel);
            return -POSIX_EINVAL;
        }
        is_zero = (timeout->tv_sec == 0 && timeout->tv_usec == 0);
        if (!is_zero && kernel && kernel->clock_now) {
            has_deadline = 1;
            deadline = deadline_from(kernel,
                                     timeout_ns(timeout->tv_sec,
                                                timeout->tv_usec, 1000));
        }
    }
    return kernel_select_core(kernel, nfds, readfds, writefds, exceptfds,
                              is_zero, has_deadline, deadline, NULL);
}

int posix_kernel_pselect(posix_kernel *kernel, int nfds,
                         posix_fd_set *readfds, posix_fd_set *writefds,
                         posix_fd_set *exceptfds,
                         const posix_timespec *timeout,
                         const posix_sigset *sigmask) {
    int is_zero = 0, has_deadline = 0;
    uint64_t deadline = 0;
    if (timeout) {
        if (posix_timespec_validate(timeout) != 0) {
            posix_kernel_cancel_wait(kernel);
            return -POSIX_EINVAL;
        }
        is_zero = (timeout->tv_sec == 0 && timeout->tv_nsec == 0);
        if (!is_zero && kernel && kernel->clock_now) {
            has_deadline = 1;
            deadline = deadline_from(kernel,
                                     timeout_ns(timeout->tv_sec,
                                                timeout->tv_nsec, 1));
        }
    }
    return kernel_select_core(kernel, nfds, readfds, writefds, exceptfds,
                              is_zero, has_deadline, deadline, sigmask);
}
