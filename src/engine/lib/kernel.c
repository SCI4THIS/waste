#include "include/kernel.h"
#include "include/select.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* --- Internal helpers --- */

static void metadata_set_now(const posix_kernel *kernel,
                             posix_path_metadata *metadata) {
    uint64_t now = posix_kernel_realtime_now(kernel);
    metadata->mtime_sec = (int64_t)(now / UINT64_C(1000000000));
    metadata->mtime_nsec = (int64_t)(now % UINT64_C(1000000000));
}

static posix_ofd *ofd_alloc(posix_ofd_kind kind) {
    posix_ofd *ofd = calloc(1, sizeof(posix_ofd));
    if (!ofd) return NULL;
    ofd->status = calloc(1, sizeof(*ofd->status));
    if (!ofd->status) { free(ofd); return NULL; }
    ofd->status->refs = 1;
    ofd->kind = kind;
    ofd->ref_count = 1;
    ofd->input_handle = ofd->output_handle = -1;
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
    case POSIX_OFD_STREAM:
        free(ofd->terminal.input);
        break;
    case POSIX_OFD_REGULAR:
        posix_kernel_file_release(ofd->regular.file);
        break;
    case POSIX_OFD_NULL:
        posix_kernel_file_release(ofd->null_device.file);
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
    if (--ofd->status->refs == 0) free(ofd->status);
    free(ofd);
}

static posix_file_object *file_object_create(const uint8_t *data,
                                             size_t length,
                                             const posix_path_metadata *metadata) {
    posix_file_object *object = calloc(1, sizeof(*object));
    if (!object) return NULL;
    object->refs = 1;
    object->inode = metadata->inode;
    object->metadata = *metadata;
    int symlink = metadata->kind == POSIX_NODE_SYMLINK;
    if (length || symlink) {
        object->data = malloc(length + (size_t)symlink);
        if (!object->data) { free(object); return NULL; }
        if (length) memcpy(object->data, data, length);
        if (symlink) object->data[length] = 0;
        object->data_capacity = length;
    }
    return object;
}

static posix_path_metadata *node_metadata(posix_kernel_path_node *node) {
    return node->file ? &node->file->metadata : &node->metadata;
}

uint32_t posix_kernel_file_link_count(const posix_kernel *kernel,
                                      const posix_file_object *object) {
    uint32_t count = 0;
    if (!kernel || !object) return 0;
    for (int i = 0; i < kernel->path_node_count; i++)
        if (kernel->path_nodes[i].file == object) count++;
    return count;
}

uint64_t posix_kernel_allocate_inode(posix_kernel *kernel) {
    /* Fork shares this allocator, not mutable engine-global state. Deleted
     * entries and independently created child files cannot reuse an inode. */
    if (!kernel || !kernel->inode_sequence || kernel->inode_sequence->next == UINT64_MAX)
        return 0;
    return kernel->inode_sequence->next++;
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
            &source->objects[i].file->metadata);
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

static int kernel_wait_read(posix_kernel *kernel, int fd, int minimum);

static int terminal_wait_with_timeout(posix_kernel *kernel, int fd,
                                      uint8_t vtime, int count) {
    posix_ofd *ofd = kernel->fds[fd].ofd;
    int minimum = (ofd->terminal.termios.lflag & POSIX_TERMIOS_LFLAG_ICANON) ?
        0 : ofd->terminal.termios.cc[POSIX_TERMIOS_VMIN];
    if (minimum > count) minimum = count;
    int result = kernel_wait_read(kernel, fd, minimum);
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
    return length;
}

static int path_normalize_from(const char *base, const uint8_t *path,
                               size_t length, char *out) {
    if (!base || !path || !out || length == 0 || length >= POSIX_PATH_MAX)
        return -POSIX_EINVAL;
    size_t cwd_length = bounded_length(base);
    if (cwd_length == 0 || cwd_length >= POSIX_PATH_MAX) return -POSIX_EINVAL;
    char combined[POSIX_PATH_MAX * 2];
    size_t combined_length = 0;
    if (path[0] != '/') {
        if (cwd_length + 1 + length >= sizeof(combined)) return -POSIX_EINVAL;
        memcpy(combined, base, cwd_length);
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

static int path_normalize(const posix_kernel *kernel, const uint8_t *path,
                          size_t length, char *out) {
    return kernel ? path_normalize_from(kernel->cwd, path, length, out) :
        -POSIX_EINVAL;
}

static posix_kernel_path_node *path_find(posix_kernel *kernel,
                                         const char *normalized) {
    for (int i = 0; i < kernel->path_node_count; i++)
        if (strcmp(kernel->path_nodes[i].path, normalized) == 0)
            return &kernel->path_nodes[i];
    return NULL;
}

static int path_search_directory(posix_kernel *kernel, const char *path) {
    posix_kernel_path_node *node = path_find(kernel, path);
    if (!node) return -POSIX_ENOENT;
    if (node->metadata.kind != POSIX_NODE_DIRECTORY) return -POSIX_ENOTDIR;
    const posix_path_metadata *metadata = node_metadata(node);
    if (kernel->uid != 0) {
        uint32_t permissions = metadata->uid == kernel->uid ?
            metadata->mode >> 6 : (metadata->gid == kernel->gid ?
                                   metadata->mode >> 3 : metadata->mode);
        if (!(permissions & 1u)) return -POSIX_EACCES;
    }
    return 0;
}

/* Walk components before consuming ..; relative links start at their parent.
 * missing_final is reserved for namespace creation. Input is never retained. */
static int path_resolve_at(posix_kernel *kernel, int directory,
                           const uint8_t *path, size_t length, int follow,
                           int missing_final, char *out) {
    if (!kernel || !path || !out) return -POSIX_EFAULT;
    if (!length || length >= POSIX_PATH_MAX) return -POSIX_EINVAL;
    const char *base = kernel->cwd;
    if (path[0] != '/' && directory != POSIX_AT_FDCWD) {
        if (!fd_valid(directory) || !kernel->fds[directory].ofd)
            return -POSIX_EBADF;
        const posix_ofd *ofd = kernel->fds[directory].ofd;
        if (ofd->kind != POSIX_OFD_DIRECTORY || !ofd->directory.node)
            return -POSIX_ENOTDIR;
        base = ofd->directory.node->path;
    }
    char pending[POSIX_PATH_MAX];
    for (size_t i = 0; i < length; i++) {
        if (!path[i]) return -POSIX_EINVAL;
        pending[i] = (char)path[i];
    }
    pending[length] = 0;
    memcpy(out, path[0] == '/' ? "/" : base,
           path[0] == '/' ? 2 : strlen(base) + 1);
    size_t at = 0;
    unsigned links = 0;
    while (pending[at]) {
        while (pending[at] == '/') at++;
        if (!pending[at]) {
            if (strcmp(out, "/") != 0) {
                int search = path_search_directory(kernel, out);
                if (search < 0) return search;
            }
            break;
        }
        int search = path_search_directory(kernel, out);
        if (search < 0) return search;
        size_t start = at;
        while (pending[at] && pending[at] != '/') at++;
        size_t part = at - start;
        size_t next = at;
        while (pending[next] == '/') next++;
        int last = !pending[next];
        if (part == 1 && pending[start] == '.') continue;
        if (part == 2 && pending[start] == '.' && pending[start + 1] == '.') {
            char *separator = out + strlen(out);
            while (separator > out && *separator != '/') separator--;
            if (separator == out) out[1] = 0;
            else if (separator) *separator = 0;
            continue;
        }
        char candidate[POSIX_PATH_NODE_NAME_MAX];
        size_t base_length = strlen(out);
        size_t prefix = base_length + (base_length > 1);
        if (prefix + part >= sizeof(candidate)) return -POSIX_ENAMETOOLONG;
        memcpy(candidate, out, base_length);
        if (base_length > 1) candidate[base_length] = '/';
        memcpy(candidate + prefix, pending + start, part);
        candidate[prefix + part] = 0;
        posix_kernel_path_node *node = path_find(kernel, candidate);
        if (!node) {
            if (!last || !missing_final) return -POSIX_ENOENT;
            memcpy(out, candidate, prefix + part + 1);
            return 0;
        }
        if (node->metadata.kind == POSIX_NODE_SYMLINK && (!last || follow)) {
            if (++links > 8) return -POSIX_ELOOP;
            if (!node->file || !node->file->data || !node->file->data[0])
                return -POSIX_ENOENT;
            const char *link_target = (const char *)node->file->data;
            size_t target = node->file->data_capacity;
            size_t rest = strlen(pending + at);
            if (target + rest >= sizeof(pending)) return -POSIX_ENAMETOOLONG;
            memmove(pending + target, pending + at, rest + 1);
            memcpy(pending, link_target, target);
            if (pending[0] == '/') memcpy(out, "/", 2);
            at = 0;
            continue;
        }
        if (pending[at] && node->metadata.kind != POSIX_NODE_DIRECTORY)
            return -POSIX_ENOTDIR;
        memcpy(out, candidate, prefix + part + 1);
    }
    return 0;
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

static int path_install_object(posix_kernel *kernel, const char *path,
                                const posix_path_metadata *metadata,
                                const uint8_t *data, size_t data_length) {
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    int result = path_normalize(kernel, (const uint8_t *)path,
                                bounded_length(path), normalized);
    if (result < 0) return result;
    posix_kernel_path_node *node = path_find(kernel, normalized);
    if (!node && kernel->path_node_count >= POSIX_PATH_NODE_MAX)
        return -POSIX_ENOMEM;
    posix_file_object *file = metadata->kind != POSIX_NODE_DIRECTORY ?
        file_object_create(data, data_length, metadata) : NULL;
    if (metadata->kind != POSIX_NODE_DIRECTORY && !file) return -POSIX_ENOMEM;
    if (!node) {
        node = &kernel->path_nodes[kernel->path_node_count++];
        memset(node, 0, sizeof(*node));
        memcpy(node->path, normalized, strlen(normalized) + 1);
    }
    posix_kernel_file_release(node->file);
    node->file = file;
    node->metadata = *metadata;
    if (file) file->metadata.size = (int64_t)data_length;
    if (kernel->inode_sequence && metadata->inode >= kernel->inode_sequence->next)
        kernel->inode_sequence->next = metadata->inode == UINT64_MAX ?
            UINT64_MAX : metadata->inode + 1;
    return 0;
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
    return path_install_object(kernel, path, metadata, data, data_length);
}

int posix_kernel_path_add_symlink(posix_kernel *kernel, const char *path,
                                  const posix_path_metadata *metadata,
                                  const char *target) {
    if (!kernel || !path || !target || !metadata ||
        metadata->kind != POSIX_NODE_SYMLINK ||
        posix_path_metadata_validate(metadata) < 0) return -POSIX_EINVAL;
    size_t target_length = 0;
    while (target_length < POSIX_PATH_MAX && target[target_length]) target_length++;
    if (!target_length) return -POSIX_ENOENT;
    if (target_length == POSIX_PATH_MAX) return -POSIX_ENAMETOOLONG;
    return path_install_object(kernel, path, metadata,
                                (const uint8_t *)target, target_length);
}

int posix_kernel_linkat(posix_kernel *kernel,
                        int old_directory, const uint8_t *old_path, size_t old_length,
                        int new_directory, const uint8_t *new_path, size_t new_length,
                        int flags) {
    if (!kernel || !old_path || !new_path) return -POSIX_EFAULT;
    if (flags & ~POSIX_AT_SYMLINK_FOLLOW) return -POSIX_EINVAL;
    if (!old_length || !new_length) return -POSIX_ENOENT;
    char source[POSIX_PATH_NODE_NAME_MAX], destination[POSIX_PATH_NODE_NAME_MAX];
    int result = path_resolve_at(kernel, old_directory, old_path, old_length,
                                  !!(flags & POSIX_AT_SYMLINK_FOLLOW), 0, source);
    if (result < 0) return result;
    posix_kernel_path_node *from = path_find(kernel, source);
    if (!from) return -POSIX_ENOENT;
    if (from->metadata.kind == POSIX_NODE_DIRECTORY) return -POSIX_EPERM;
    result = path_resolve_at(kernel, new_directory, new_path, new_length,
                             0, 1, destination);
    if (result < 0) return result;
    if (path_find(kernel, destination)) return -POSIX_EEXIST;
    if (new_path[new_length - 1] == '/') return -POSIX_ENOENT;
    if (!from->file) return -POSIX_EINVAL;
    if (kernel->path_node_count >= POSIX_PATH_NODE_MAX) return -POSIX_ENOMEM;
    posix_kernel_path_node *to = &kernel->path_nodes[kernel->path_node_count++];
    memset(to, 0, sizeof(*to));
    memcpy(to->path, destination, strlen(destination) + 1);
    to->metadata = *node_metadata(from);
    to->file = from->file;
    posix_file_object_retain(to->file);
    return 0;
}

int posix_kernel_symlinkat(posix_kernel *kernel,
                           const uint8_t *target, size_t target_length,
                           int directory, const uint8_t *path, size_t length) {
    if (!kernel || !target || !path) return -POSIX_EFAULT;
    if (!target_length || !length) return -POSIX_ENOENT;
    if (target_length >= POSIX_PATH_MAX) return -POSIX_ENAMETOOLONG;
    for (size_t i = 0; i < target_length; i++)
        if (!target[i]) return -POSIX_EINVAL;
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    int result = path_resolve_at(kernel, directory, path, length, 0, 1, normalized);
    if (result < 0) return result;
    if (path_find(kernel, normalized)) return -POSIX_EEXIST;
    if (path[length - 1] == '/') return -POSIX_ENOENT;
    char copy[POSIX_PATH_MAX];
    memcpy(copy, target, target_length);
    copy[target_length] = 0;
    posix_path_metadata metadata = { POSIX_NODE_SYMLINK, 0777, 0, 0,
        (int64_t)target_length, posix_kernel_allocate_inode(kernel), 0, 0 };
    metadata_set_now(kernel, &metadata);
    return posix_kernel_path_add_symlink(kernel, normalized, &metadata, copy);
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
    int result = path_resolve_at(kernel, POSIX_AT_FDCWD, path, length, 0, 1, normalized);
    if (result < 0) return result;
    if (path_find(kernel, normalized)) return -POSIX_EEXIST;
    if (path_prefix_is_file(kernel, normalized)) return -POSIX_ENOTDIR;
    posix_path_metadata metadata = { POSIX_NODE_DIRECTORY,
        (uint32_t)(mode ? mode : 0777), 0, 0, 0,
        posix_kernel_allocate_inode(kernel), 0, 0 };
    metadata_set_now(kernel, &metadata);
    return posix_kernel_path_add_data(kernel, normalized, &metadata, NULL, 0);
}

int posix_kernel_path_unlink(posix_kernel *kernel, const uint8_t *path,
                             size_t length, int directory) {
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    int result = path_resolve_at(kernel, POSIX_AT_FDCWD, path, length, 0, 0, normalized);
    if (result < 0) return result;
    posix_kernel_path_node *node = path_find(kernel, normalized);
    if (!node) return path_prefix_is_file(kernel, normalized) ?
        -POSIX_ENOTDIR : -POSIX_ENOENT;
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
    int index = (int)(node - kernel->path_nodes);
    /* Compaction moves namespace entries, but open descriptions keep their
     * file objects. Rebase surviving entries once in ascending order (also
     * safe for dup aliases); a removed entry no longer supplies metadata. */
    for (int i = index; i < kernel->path_node_count; i++) {
        for (int fd = 0; fd < POSIX_KERNEL_FD_MAX; fd++) {
            posix_ofd *ofd = kernel->fds[fd].ofd;
            if (!ofd) continue;
            posix_kernel_path_node **entry = NULL;
            if (ofd->kind == POSIX_OFD_REGULAR) entry = &ofd->regular.node;
            else if (ofd->kind == POSIX_OFD_DIRECTORY) entry = &ofd->directory.node;
            if (entry && *entry == &kernel->path_nodes[i])
                *entry = i == index ? NULL : &kernel->path_nodes[i - 1];
        }
    }
    posix_kernel_file_release(node->file);
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
    int result = path_resolve_at(kernel, POSIX_AT_FDCWD, source, source_length,
                                  0, 0, source_name);
    if (result < 0) return result;
    result = path_resolve_at(kernel, POSIX_AT_FDCWD, destination, destination_length,
                             0, 1, destination_name);
    if (result < 0) return result;
    posix_kernel_path_node *from = path_find(kernel, source_name);
    posix_kernel_path_node *to = path_find(kernel, destination_name);
    if (!from) return path_prefix_is_file(kernel, source_name) ?
        -POSIX_ENOTDIR : -POSIX_ENOENT;
    if (from == to || (to && from->file && from->file == to->file)) return 0;
    if (to) {
        if (to->metadata.kind == POSIX_NODE_DIRECTORY) return -POSIX_EISDIR;
        if (from->metadata.kind == POSIX_NODE_DIRECTORY) return -POSIX_ENOTDIR;
        result = posix_kernel_path_unlink(kernel, (const uint8_t *)destination_name,
                                          strlen(destination_name), 0);
        if (result < 0) return result;
        /* Unlink compacts entries and preserves existing open descriptions. */
        from = path_find(kernel, source_name);
        if (!from) return -POSIX_ENOENT;
    }
    if (path_prefix_is_file(kernel, destination_name)) return -POSIX_ENOTDIR;
    size_t length = strlen(destination_name);
    memcpy(from->path, destination_name, length + 1);
    return 0;
}

int posix_kernel_path_readlink(posix_kernel *kernel, const uint8_t *path,
                               size_t length, char *buffer, size_t capacity) {
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    if (!buffer || capacity == 0) return -POSIX_EINVAL;
    int result = path_resolve_at(kernel, POSIX_AT_FDCWD, path, length, 0, 0, normalized);
    if (result < 0) return result;
    posix_kernel_path_node *node = path_find(kernel, normalized);
    if (!node) return path_prefix_is_file(kernel, normalized) ?
        -POSIX_ENOTDIR : -POSIX_ENOENT;
    if (node->metadata.kind != POSIX_NODE_SYMLINK || !node->file)
        return -POSIX_EINVAL;
    size_t target_length = node->file->data_capacity;
    if (target_length > capacity) target_length = capacity;
    memcpy(buffer, node->file->data, target_length);
    return (int)target_length;
}

int posix_kernel_realpath(posix_kernel *kernel, const uint8_t *path,
                          size_t length, char *buffer, size_t capacity) {
    char resolved[POSIX_PATH_NODE_NAME_MAX];
    if (!kernel || !path || !buffer) return -POSIX_EFAULT;
    if (!length) return -POSIX_ENOENT;
    if (length >= POSIX_PATH_MAX) return -POSIX_ENAMETOOLONG;
    int result = path_resolve_at(kernel, POSIX_AT_FDCWD, path, length,
                                 1, 0, resolved);
    if (result < 0) return result;
    if (!path_find(kernel, resolved)) return -POSIX_ENOENT;
    size_t resolved_length = strlen(resolved);
    if (capacity <= resolved_length) return -POSIX_ERANGE;
    memcpy(buffer, resolved, resolved_length + 1);
    return (int)resolved_length;
}

int posix_kernel_path_set_cwd(posix_kernel *kernel, const char *path) {
    if (!kernel || !path) return -POSIX_EINVAL;
    size_t length = bounded_length(path);
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    int result = path_resolve_at(kernel, POSIX_AT_FDCWD, (const uint8_t *)path,
                                 length, 1, 0, normalized);
    if (result < 0) return result;
    posix_kernel_path_node *node = path_find(kernel, normalized);
    if (!node) return path_prefix_is_file(kernel, normalized) ? -POSIX_ENOTDIR : -POSIX_ENOENT;
    if (node->metadata.kind != POSIX_NODE_DIRECTORY) return -POSIX_ENOTDIR;
    memcpy(kernel->cwd, normalized, strlen(normalized) + 1);
    return 0;
}

int posix_kernel_path_set_mtime(posix_kernel *kernel, const uint8_t *path,
                                size_t length, int64_t seconds,
                                int64_t nanoseconds) {
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    if (!kernel || !path || length == 0 || nanoseconds < 0 ||
        nanoseconds >= 1000000000)
        return -POSIX_EINVAL;
    int result = path_resolve_at(kernel, POSIX_AT_FDCWD, path, length, 1, 0, normalized);
    if (result < 0) return result;
    posix_kernel_path_node *node = path_find(kernel, normalized);
    if (!node) return path_prefix_is_file(kernel, normalized) ?
        -POSIX_ENOTDIR : -POSIX_ENOENT;
    node_metadata(node)->mtime_sec = seconds;
    node_metadata(node)->mtime_nsec = nanoseconds;
    return 0;
}

int posix_kernel_path_chmod(posix_kernel *kernel, const uint8_t *path,
                            size_t length, uint32_t mode) {
    return posix_kernel_fchmodat(kernel, POSIX_AT_FDCWD, path, length, mode, 0);
}

int posix_kernel_fchmodat(posix_kernel *kernel, int directory,
                          const uint8_t *path, size_t length,
                          uint32_t mode, int flags) {
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    if (!kernel) return -POSIX_EINVAL;
    if (flags & ~POSIX_AT_SYMLINK_NOFOLLOW) return -POSIX_EINVAL;
    if (!length) return -POSIX_ENOENT;
    int result = path_resolve_at(kernel, directory, path, length,
        !(flags & POSIX_AT_SYMLINK_NOFOLLOW), 0, normalized);
    if (result < 0) return result;
    posix_kernel_path_node *node = path_find(kernel, normalized);
    if (!node) return -POSIX_ENOENT;
    if (node->metadata.kind == POSIX_NODE_SYMLINK) return -POSIX_EOPNOTSUPP;
    node_metadata(node)->mode = mode & 07777u;
    return 0;
}

static int path_stat_at(posix_kernel *kernel, int directory,
                         const uint8_t *path, size_t length, int follow,
                         posix_path_metadata *metadata, uint32_t *links) {
    if (!kernel || !metadata) return -POSIX_EFAULT;
    if (length >= POSIX_PATH_MAX) return -POSIX_EINVAL;
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    /* A trailing slash requests a directory, even for lstat. */
    int result = path_resolve_at(kernel, directory, path, length,
        follow || (length && path && path[length - 1] == '/'), 0, normalized);
    if (result < 0) return result;
    posix_kernel_path_node *node = path_find(kernel, normalized);
    if (!node) return -POSIX_ENOENT;
    *metadata = *node_metadata(node);
    if (links) *links = node->file ? posix_kernel_file_link_count(kernel, node->file) : 2;
    if (node->metadata.kind == POSIX_NODE_REGULAR && node->file)
        metadata->size = (int64_t)node->file->data_capacity;
    return 0;
}

int posix_kernel_path_stat(posix_kernel *kernel, const uint8_t *path,
                           size_t length, int follow,
                           posix_path_metadata *metadata) {
    return path_stat_at(kernel, POSIX_AT_FDCWD, path, length, follow, metadata, NULL);
}

int posix_kernel_fstatat_nlink(posix_kernel *kernel, int directory,
                               const uint8_t *path, size_t length, int flags,
                               posix_path_metadata *metadata, uint32_t *links) {
    if (flags & ~POSIX_AT_SYMLINK_NOFOLLOW) return -POSIX_EINVAL;
    if (!length) return -POSIX_ENOENT;
    return path_stat_at(kernel, directory, path, length,
                        !(flags & POSIX_AT_SYMLINK_NOFOLLOW), metadata, links);
}

int posix_kernel_fstatat(posix_kernel *kernel, int directory,
                         const uint8_t *path, size_t length, int flags,
                         posix_path_metadata *metadata) {
    return posix_kernel_fstatat_nlink(kernel, directory, path, length,
                                     flags, metadata, NULL);
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

static int path_snapshot(posix_kernel *kernel, const uint8_t *path,
                         size_t length, size_t maximum_size,
                         uint32_t required_mode, uint8_t **data_out,
                         size_t *length_out,
                         posix_path_metadata *metadata_out,
                         char *resolved_path, size_t path_capacity) {
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    posix_kernel_path_node *node;
    uint8_t *copy = NULL;
    int result;
    if (!kernel || !path || !data_out || !length_out || !metadata_out)
        return -POSIX_EFAULT;
    *data_out = NULL;
    *length_out = 0;
    result = path_resolve_at(kernel, POSIX_AT_FDCWD, path, length, 1, 0, normalized);
    if (result < 0) return result;
    node = path_find(kernel, normalized);
    if (!node) return -POSIX_ENOENT;
    if (node->metadata.kind == POSIX_NODE_DIRECTORY)
        return -POSIX_EISDIR;
    if (node->metadata.kind != POSIX_NODE_REGULAR)
        return -POSIX_ENOENT;
    if (required_mode && !(node_metadata(node)->mode & required_mode))
        return -POSIX_EACCES;
    if (maximum_size != 0 && node->file->data_capacity > maximum_size)
        return -POSIX_E2BIG;
    size_t resolved_length = strlen(node->path);
    if (resolved_path && path_capacity <= resolved_length)
        return -POSIX_ERANGE;
    if (node->file->data_capacity > 0) {
        copy = (uint8_t *)malloc(node->file->data_capacity);
        if (!copy) return -POSIX_ENOMEM;
        memcpy(copy, node->file->data, node->file->data_capacity);
    }
    *data_out = copy;
    *length_out = node->file->data_capacity;
    *metadata_out = *node_metadata(node);
    metadata_out->size = (int64_t)node->file->data_capacity;
    if (resolved_path)
        memcpy(resolved_path, node->path, resolved_length + 1);
    return 0;
}

int posix_kernel_path_snapshot(posix_kernel *kernel, const uint8_t *path,
                               size_t length, size_t maximum_size,
                               uint8_t **data_out, size_t *length_out,
                               posix_path_metadata *metadata_out) {
    return path_snapshot(kernel, path, length, maximum_size, 0111u,
                         data_out, length_out, metadata_out, NULL, 0);
}

int posix_kernel_path_read_snapshot(posix_kernel *kernel,
                                    const uint8_t *path, size_t length,
                                    size_t maximum_size, uint8_t **data_out,
                                    size_t *length_out,
                                    posix_path_metadata *metadata_out) {
    return path_snapshot(kernel, path, length, maximum_size, 0444u,
                         data_out, length_out, metadata_out, NULL, 0);
}

int posix_kernel_path_image_snapshot(posix_kernel *kernel,
                                     const uint8_t *path, size_t length,
                                     size_t maximum_size, int executable,
                                     uint8_t **data_out, size_t *length_out,
                                     posix_path_metadata *metadata_out,
                                     char *resolved_path, size_t path_capacity) {
    if (!resolved_path) return -POSIX_EFAULT;
    return path_snapshot(kernel, path, length, maximum_size,
                         executable ? 0111u : 0444u, data_out, length_out,
                         metadata_out, resolved_path, path_capacity);
}

/* --- Lifecycle --- */

/* Attach stdio without replacing the kernel or its mounted filesystem. */
int posix_kernel_attach_terminal(posix_kernel *k) {
    if (!k) return -POSIX_EINVAL;
    if (k->fds[0].ofd || k->fds[1].ofd || k->fds[2].ofd) return -POSIX_EBUSY;
    posix_ofd *tty = ofd_alloc(POSIX_OFD_TERMINAL);
    if (!tty) return -POSIX_ENOMEM;
    tty->terminal.input = malloc(POSIX_TERMINAL_INPUT_CAPACITY);
    if (!tty->terminal.input) { ofd_release(tty); return -POSIX_ENOMEM; }
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
    tty->terminal.foreground_pgid = k->process_group_id;
    tty->status->flags = POSIX_O_RDWR;
    tty->terminal.controlling_session_id = k->session_id;
    k->has_controlling_terminal = 1;
    /* fds 0, 1, 2 share the same terminal OFD */
    tty->ref_count = 3;
    k->fds[0].ofd = tty;
    k->fds[1].ofd = tty;
    k->fds[2].ofd = tty;
    return 0;
}

int posix_kernel_attach_stream(posix_kernel *k, int fd, int terminal,
                                int input_handle, int output_handle,
                                uint32_t mode) {
    if (!k || !fd_valid(fd) || (input_handle < 0 && output_handle < 0))
        return -POSIX_EINVAL;
    if (k->fds[fd].ofd) return -POSIX_EBUSY;
    posix_ofd *ofd = ofd_alloc(terminal ? POSIX_OFD_TERMINAL : POSIX_OFD_STREAM);
    if (!ofd) return -POSIX_ENOMEM;
    ofd->input_handle = input_handle;
    ofd->output_handle = output_handle;
    ofd->stream_mode = mode;
    ofd->status->flags = input_handle < 0 ? POSIX_O_WRONLY :
        output_handle < 0 ? 0 : POSIX_O_RDWR;
    if (input_handle >= 0) {
        ofd->terminal.input = malloc(POSIX_TERMINAL_INPUT_CAPACITY);
        if (!ofd->terminal.input) { ofd_release(ofd); return -POSIX_ENOMEM; }
        ofd->terminal.input_capacity = POSIX_TERMINAL_INPUT_CAPACITY;
    }
    ofd->terminal.termios.cc[POSIX_TERMIOS_VINTR] = 3;
    ofd->terminal.termios.cc[POSIX_TERMIOS_VEOF] = 4;
    ofd->terminal.termios.cc[POSIX_TERMIOS_VERASE] = 127;
    ofd->terminal.termios.cc[POSIX_TERMIOS_VKILL] = 21;
    ofd->terminal.termios.cc[POSIX_TERMIOS_VMIN] = 1;
    ofd->terminal.winsize.rows = 24;
    ofd->terminal.winsize.columns = 80;
    ofd->terminal.foreground_pgid = k->process_group_id;
    if (terminal) {
        ofd->terminal.controlling_session_id = k->session_id;
        k->has_controlling_terminal = 1;
    }
    k->fds[fd].ofd = ofd;
    return 0;
}

/* Process-associated advisory locks live on retained inodes. Owner references
 * belong to kernels, not records: records retain the inode and are removed
 * when the last transactional kernel view of their owner is destroyed. */
struct posix_record_lock {
    posix_file_object *file;
    posix_lock_owner *owner;
    uint64_t start, end; /* inclusive; UINT64_MAX means all future bytes */
    int type;
    posix_record_lock *file_next, *owner_next;
};

struct posix_lock_owner {
    uint32_t refs;
    int pid;
    posix_record_lock *records;
    posix_kernel *waiting_kernel; /* borrowed while that kernel retains owner */
};

static posix_lock_owner *lock_owner_create(int pid) {
    posix_lock_owner *owner = calloc(1, sizeof(*owner));
    if (owner) { owner->refs = 1; owner->pid = pid; }
    return owner;
}

static void record_remove(posix_record_lock *record) {
    posix_file_object *file = record->file;
    posix_record_lock **at = &file->locks;
    while (*at != record) at = &(*at)->file_next;
    *at = record->file_next;
    at = &record->owner->records;
    while (*at != record) at = &(*at)->owner_next;
    *at = record->owner_next;
    file->lock_count--;
    free(record);
    posix_kernel_file_release(file);
}

static void lock_owner_release(posix_lock_owner *owner) {
    if (!owner || --owner->refs) return;
    while (owner->records) record_remove(owner->records);
    free(owner);
}

static void record_release_file(posix_lock_owner *owner, posix_file_object *file) {
    posix_record_lock *record = file->locks;
    while (record) {
        posix_record_lock *next = record->file_next;
        if (record->owner == owner) record_remove(record);
        record = next;
    }
}

int posix_kernel_set_lock_pid(posix_kernel *kernel, int pid) {
    if (!kernel || pid <= 0) return -POSIX_EINVAL;
    if (kernel->lock_owner && kernel->lock_owner->pid == pid) return 0;
    posix_lock_owner *owner = lock_owner_create(pid);
    if (!owner) return -POSIX_ENOMEM;
    posix_kernel_cancel_wait(kernel);
    lock_owner_release(kernel->lock_owner);
    kernel->lock_owner = owner;
    return 0;
}

static int record_overlap(uint64_t start, uint64_t end,
                           const posix_record_lock *record) {
    return start <= record->end && record->start <= end;
}

static posix_record_lock *record_conflict(posix_file_object *file,
        posix_lock_owner *owner, int type, uint64_t start, uint64_t end) {
    for (posix_record_lock *record = file->locks; record; record = record->file_next)
        if (record->owner != owner && record_overlap(start, end, record) &&
            (type == POSIX_F_WRLCK || record->type == POSIX_F_WRLCK)) return record;
    return NULL;
}

static void record_pending_free(posix_record_lock *pending) {
    while (pending) {
        posix_record_lock *next = pending->file_next;
        free(pending);
        pending = next;
    }
}

static int record_pending_add(posix_record_lock **pending, int type,
                               uint64_t start, uint64_t end) {
    posix_record_lock *record = calloc(1, sizeof(*record));
    if (!record) return -POSIX_ENOLCK;
    record->type = type;
    record->start = start;
    record->end = end;
    record->file_next = *pending;
    *pending = record;
    return 0;
}

static int record_replace(posix_file_object *file, posix_lock_owner *owner,
                            int type, uint64_t start, uint64_t end) {
    posix_record_lock *pending = NULL;
    uint32_t others = 0;
    for (posix_record_lock *old = file->locks; old; old = old->file_next) {
        if (old->owner != owner) { others++; continue; }
        if (!record_overlap(start, end, old)) {
            if (record_pending_add(&pending, old->type, old->start, old->end) < 0)
                goto no_resources;
        } else {
            if (old->start < start &&
                record_pending_add(&pending, old->type, old->start, start - 1) < 0)
                goto no_resources;
            if (old->end > end &&
                record_pending_add(&pending, old->type, end + 1, old->end) < 0)
                goto no_resources;
        }
    }
    if (type != POSIX_F_UNLCK && record_pending_add(&pending, type, start, end) < 0)
        goto no_resources;
    /* Coalesce adjacent or overlapping records of the same owner/type before
     * admission. Preparation is private until all allocations/limits pass. */
    for (posix_record_lock *one = pending; one; one = one->file_next) {
        posix_record_lock **at = &one->file_next;
        while (*at) {
            posix_record_lock *two = *at;
            if (one->type == two->type &&
                (one->end == UINT64_MAX || two->start <= one->end + 1) &&
                (two->end == UINT64_MAX || one->start <= two->end + 1)) {
                if (two->start < one->start) one->start = two->start;
                if (two->end > one->end) one->end = two->end;
                *at = two->file_next;
                free(two);
                at = &one->file_next;
            } else at = &two->file_next;
        }
    }
    uint32_t count = 0;
    for (posix_record_lock *record = pending; record; record = record->file_next) count++;
    if (count > POSIX_RECORD_LOCK_MAX || others > POSIX_RECORD_LOCK_MAX - count ||
        file->refs > UINT32_MAX - count) goto no_resources;
    record_release_file(owner, file);
    while (pending) {
        posix_record_lock *record = pending;
        pending = record->file_next;
        record->owner = owner;
        record->file = file;
        posix_file_object_retain(file);
        record->file_next = file->locks;
        file->locks = record;
        record->owner_next = owner->records;
        owner->records = record;
        file->lock_count++;
    }
    return 0;
no_resources:
    record_pending_free(pending);
    return -POSIX_ENOLCK;
}

static int record_wait_cycle(posix_lock_owner *owner, posix_lock_owner *target,
                              posix_lock_owner **visited, unsigned *count) {
    if (owner == target) return 1;
    for (unsigned i = 0; i < *count; i++) if (visited[i] == owner) return 0;
    if (*count == POSIX_RECORD_LOCK_MAX) return -POSIX_ENOLCK;
    visited[(*count)++] = owner;
    posix_kernel *kernel = owner->waiting_kernel;
    if (!kernel || !kernel->wait.active || !kernel->wait.lock_file) return 0;
    posix_wait_record *wait = &kernel->wait;
    for (posix_record_lock *record = wait->lock_file->locks; record;
         record = record->file_next) {
        if (record->owner == owner || !record_overlap(wait->lock_start, wait->lock_end, record) ||
            (wait->lock_type != POSIX_F_WRLCK && record->type != POSIX_F_WRLCK)) continue;
        int result = record_wait_cycle(record->owner, target, visited, count);
        if (result) return result;
    }
    return 0;
}

posix_kernel *posix_kernel_create(int interactive) {
    posix_kernel *k = calloc(1, sizeof(posix_kernel));
    if (!k) return NULL;
    k->lock_owner = lock_owner_create(1);
    if (!k->lock_owner) { free(k); return NULL; }
    k->shm_namespace = posix_shm_namespace_create();
    if (!k->shm_namespace) { lock_owner_release(k->lock_owner); free(k); return NULL; }
    k->inode_sequence = calloc(1, sizeof(*k->inode_sequence));
    if (!k->inode_sequence) {
        posix_shm_namespace_release(k->shm_namespace);
        lock_owner_release(k->lock_owner);
        free(k);
        return NULL;
    }
    k->inode_sequence->refs = 1;
    /* Transport catalogues number their bounded entries from one. Keep
     * synthesized devices and later guest-created inodes outside that range. */
    k->inode_sequence->next = (uint64_t)VFS_MAX_ENTRIES + 1;

    memcpy(k->cwd, "/", 2);
    k->process_group_id = 1;
    k->session_id = 1;
    k->creation_mask = 0022;
    const posix_path_metadata root = {
        POSIX_NODE_DIRECTORY, 0755, 0, 0, 0, 1, 0, 0
    };
    const posix_path_metadata directory = {
        POSIX_NODE_DIRECTORY, 0755, 0, 0, 0, 2, 0, 0
    };
    const posix_path_metadata devices = {
        POSIX_NODE_DIRECTORY, 0755, 0, 0, 0, posix_kernel_allocate_inode(k), 0, 0
    };
    const posix_path_metadata null_device = {
        POSIX_NODE_NULL, 0666, 0, 0, 0, posix_kernel_allocate_inode(k), 0, 0
    };
    if (posix_kernel_path_add(k, "/", &root) < 0 ||
        posix_kernel_path_add(k, "/bin", &directory) < 0 ||
        posix_kernel_path_add(k, "/usr", &directory) < 0 ||
        posix_kernel_path_add(k, "/usr/bin", &directory) < 0 ||
        posix_kernel_path_add(k, "/tmp", &directory) < 0 ||
        posix_kernel_path_add(k, "/dev", &devices) < 0 ||
        posix_kernel_path_add(k, "/dev/null", &null_device) < 0) {
        posix_kernel_destroy(k);
        return NULL;
    }

    if (interactive && posix_kernel_attach_terminal(k)) {
        posix_kernel_destroy(k);
        return NULL;
    }
    return k;
}

void posix_kernel_destroy(posix_kernel *kernel);
posix_kernel *posix_kernel_clone(const posix_kernel *source);

posix_kernel *posix_kernel_clone(const posix_kernel *source) {
    if (!source || !source->lock_owner || source->lock_owner->refs == UINT32_MAX)
        return NULL;
    posix_kernel *clone = calloc(1, sizeof(*clone));
    if (!clone) return NULL;
    memcpy(clone, source, sizeof(*clone));
    clone->lock_owner->refs++;
    posix_shm_namespace_retain(clone->shm_namespace);
    clone->inode_sequence->refs++;
    for (int i = 0; i < clone->path_node_count; i++)
        posix_file_object_retain(clone->path_nodes[i].file);
    memset(clone->fds, 0, sizeof(clone->fds));
    clone->wait.active = 0;
    clone->wait.read_minimum = 0;
    clone->wait.lock_file = NULL;
    /* Dispositions and masks are inherited; queued process signals are not. */
    memset(&clone->pending_signals, 0, sizeof(clone->pending_signals));
    clone->delivered_signal = 0;
    const posix_ofd *regular_sources[POSIX_KERNEL_FD_MAX] = {0};
    posix_ofd *regular_clones[POSIX_KERNEL_FD_MAX] = {0};
    int regular_count = 0;
    for (int fd = 0; fd < POSIX_KERNEL_FD_MAX; fd++) {
        const posix_ofd *source_ofd = source->fds[fd].ofd;
        if (!source_ofd) continue;
        clone->fds[fd].cloexec = source->fds[fd].cloexec;
        posix_ofd *shared = (posix_ofd *)source_ofd;
        if (shared->kind == POSIX_OFD_REGULAR || shared->kind == POSIX_OFD_DIRECTORY) {
            posix_kernel_path_node *entry = shared->kind == POSIX_OFD_REGULAR ?
                shared->regular.node : shared->directory.node;
            ptrdiff_t index = entry ?
                entry - source->path_nodes : -1;
            if (entry &&
                (index < 0 || index >= source->path_node_count)) {
                posix_kernel_destroy(clone);
                return NULL;
            }
            /* Descriptor aliases within the clone share one description.
             * Rebase pathname pointers into its namespace and retain the inode.
             * Sharing fork offsets across these descriptions remains separate. */
            posix_ofd *copy = NULL;
            for (int i = 0; i < regular_count; i++)
                if (regular_sources[i] == source_ofd) copy = regular_clones[i];
            if (!copy) {
                if (regular_count >= POSIX_KERNEL_FD_MAX ||
                    shared->status->refs == UINT32_MAX) {
                    posix_kernel_destroy(clone); return NULL;
                }
                copy = malloc(sizeof(*copy));
                if (!copy) { posix_kernel_destroy(clone); return NULL; }
                *copy = *shared;
                copy->ref_count = 1;
                copy->status->refs++;
                if (shared->kind == POSIX_OFD_REGULAR) {
                    copy->regular.node = index >= 0 ? &clone->path_nodes[index] : NULL;
                    posix_file_object_retain(copy->regular.file);
                } else {
                    copy->directory.node = index >= 0 ? &clone->path_nodes[index] : NULL;
                }
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
        if (!to) {
            if (target->path_node_count >= POSIX_PATH_NODE_MAX)
                return -POSIX_ENOMEM;
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
        if (target->inode_sequence && from->metadata.inode >= target->inode_sequence->next)
            target->inode_sequence->next = from->metadata.inode == UINT64_MAX ?
                UINT64_MAX : from->metadata.inode + 1;
    }
    return 0;
}

/* Close all file descriptors without destroying the kernel.  Called when a
 * process exits to release pipe endpoints so that readers see EOF.  The
 * kernel remains alive for path-node merging at waitpid time. */
void posix_kernel_close_all_fds(posix_kernel *kernel) {
    if (!kernel) return;
    posix_kernel_cancel_wait(kernel);
    for (int i = 0; i < POSIX_KERNEL_FD_MAX; i++) {
        if (kernel->fds[i].ofd) (void)posix_kernel_close(kernel, i);
    }
}

void posix_kernel_destroy(posix_kernel *kernel) {
    if (!kernel) return;
    posix_kernel_cancel_wait(kernel);
    for (int i = 0; i < POSIX_KERNEL_FD_MAX; i++) {
        posix_ofd *ofd = kernel->fds[i].ofd;
        if (ofd) {
            kernel->fds[i].ofd = NULL;
            pipe_count_dec(ofd);
            ofd_release(ofd);
        }
    }
    for (int i = 0; i < kernel->path_node_count; i++)
        posix_kernel_file_release(kernel->path_nodes[i].file);
    if (kernel->inode_sequence && --kernel->inode_sequence->refs == 0)
        free(kernel->inode_sequence);
    posix_shm_namespace_release(kernel->shm_namespace);
    lock_owner_release(kernel->lock_owner);
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

int posix_kernel_umask(posix_kernel *kernel, int mask) {
    if (!kernel) return -POSIX_EINVAL;
    int previous = (int)kernel->creation_mask;
    kernel->creation_mask = (uint32_t)mask & 0777u;
    return previous;
}

void posix_kernel_set_clock(posix_kernel *kernel, posix_clock_now_fn clock_now,
                            void *clock_data) {
    if (!kernel) return;
    kernel->clock_now = clock_now;
    kernel->clock_data = clock_data;
}

void posix_kernel_set_realtime_clock(posix_kernel *kernel,
                                     posix_clock_now_fn realtime_now,
                                     void *realtime_data) {
    if (!kernel) return;
    kernel->realtime_now = realtime_now;
    kernel->realtime_data = realtime_data;
}

uint64_t posix_kernel_realtime_now(const posix_kernel *kernel) {
    return kernel && kernel->realtime_now ?
        kernel->realtime_now(kernel->realtime_data) : 0;
}

int posix_kernel_clock_now(const posix_kernel *kernel, int clock_id,
                           uint64_t *nanoseconds) {
    posix_clock_now_fn read_clock;
    void *clock_data;
    if (clock_id != POSIX_CLOCK_REALTIME && clock_id != POSIX_CLOCK_MONOTONIC)
        return -POSIX_EINVAL;
    if (!nanoseconds) return -POSIX_EFAULT;
    if (!kernel) return -POSIX_ENOSYS;
    read_clock = clock_id == POSIX_CLOCK_REALTIME ?
        kernel->realtime_now : kernel->clock_now;
    clock_data = clock_id == POSIX_CLOCK_REALTIME ?
        kernel->realtime_data : kernel->clock_data;
    if (!read_clock) return -POSIX_ENOSYS;
    *nanoseconds = read_clock(clock_data);
    return 0;
}

static int signal_valid(int signal) {
    return signal >= 1 && signal <= POSIX_SIGNAL_MAX;
}

void posix_kernel_alarm_poll(posix_kernel *kernel) {
    if (!kernel || !kernel->alarm_active || !kernel->clock_now) return;
    if (kernel->clock_now(kernel->clock_data) < kernel->alarm_deadline_ns) return;
    kernel->alarm_active = 0;
    (void)posix_kernel_signal_raise(kernel, POSIX_SIGALRM);
}

int64_t posix_kernel_alarm(posix_kernel *kernel, uint32_t seconds) {
    if (!kernel || !kernel->clock_now) return -POSIX_ENOSYS;
    uint64_t now = kernel->clock_now(kernel->clock_data);
    uint64_t remaining = 0;
    if (kernel->alarm_active) {
        if (now >= kernel->alarm_deadline_ns) {
            kernel->alarm_active = 0;
            (void)posix_kernel_signal_raise(kernel, POSIX_SIGALRM);
        } else {
            uint64_t ns = kernel->alarm_deadline_ns - now;
            remaining = ns / UINT64_C(1000000000) +
                        (ns % UINT64_C(1000000000) != 0);
        }
    }
    uint64_t duration = (uint64_t)seconds * UINT64_C(1000000000);
    kernel->alarm_active = seconds != 0;
    kernel->alarm_deadline_ns = now > UINT64_MAX - duration ?
        UINT64_MAX : now + duration;
    return (int64_t)remaining;
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
    /* Default notifications do not interrupt a syscall or terminate a process.
     * Stop/continue scheduling is not implemented. */
    if (kernel->signal_disposition[signal] == POSIX_SIGNAL_DEFAULT) {
        if (signal == 17 || signal == 18 || signal == 23 || signal == POSIX_SIGWINCH)
            return 0;
        if (signal >= 19 && signal <= 22) return -POSIX_ENOSYS;
    }
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

int posix_kernel_create_session(posix_kernel *kernel, int pid) {
    if (!kernel || pid <= 0) return -POSIX_EINVAL;
    kernel->session_id = pid;
    kernel->process_group_id = pid;
    kernel->has_controlling_terminal = 0;
    /* Retain all descriptors and the old terminal's shared session/foreground
     * identity. Detachment changes process ownership, not the device. */
    return pid;
}

int posix_kernel_terminal_session(const posix_kernel *kernel, int fd) {
    if (!kernel || !fd_valid(fd) || !kernel->fds[fd].ofd) return -POSIX_EBADF;
    const posix_ofd *ofd = kernel->fds[fd].ofd;
    if (ofd->kind != POSIX_OFD_TERMINAL || !kernel->has_controlling_terminal ||
        ofd->terminal.controlling_session_id != kernel->session_id)
        return -POSIX_ENOTTY;
    return ofd->terminal.controlling_session_id;
}

int posix_kernel_terminal_get_foreground_pgid(const posix_kernel *kernel,
                                              int fd) {
    if (!posix_kernel_isatty(kernel, fd)) return -POSIX_EBADF;
    int session = posix_kernel_terminal_session(kernel, fd);
    if (session < 0) return session;
    return kernel->fds[fd].ofd->terminal.foreground_pgid;
}

int posix_kernel_terminal_set_foreground_pgid(posix_kernel *kernel, int fd,
                                              int pgid) {
    if (!posix_kernel_isatty(kernel, fd) || pgid <= 0) return -POSIX_EINVAL;
    int session = posix_kernel_terminal_session(kernel, fd);
    if (session < 0) return session;
    kernel->fds[fd].ofd->terminal.foreground_pgid = pgid;
    return 0;
}

static void route_terminal_signals(posix_kernel *kernel) {
    if (!kernel || kernel->process_group_id <= 0 ||
        !kernel->has_controlling_terminal) return;
    for (int fd = 0; fd < POSIX_KERNEL_FD_MAX; fd++) {
        posix_ofd *ofd = kernel->fds[fd].ofd;
        if (!ofd || ofd->kind != POSIX_OFD_TERMINAL ||
            ofd->terminal.controlling_session_id != kernel->session_id ||
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

int posix_kernel_signal_interrupt(posix_kernel *kernel) {
    if (!kernel) return 0;
    posix_kernel_alarm_poll(kernel);
    route_terminal_signals(kernel);
    int signal = first_unmasked_pending(kernel);
    if (!signal) return 0;
    signal_bit_clear(&kernel->pending_signals, signal);
    kernel->delivered_signal = signal;
    posix_kernel_cancel_wait(kernel);
    return -POSIX_EINTR;
}

int posix_kernel_signal_default_pending(posix_kernel *kernel) {
    if (!kernel) return 0;
    posix_kernel_alarm_poll(kernel);
    route_terminal_signals(kernel);
    for (int signal = 1; signal <= POSIX_SIGNAL_MAX; signal++)
        if (signal_bit_test(&kernel->pending_signals, signal) &&
            !signal_bit_test(&kernel->signal_mask, signal) &&
            kernel->signal_disposition[signal] == POSIX_SIGNAL_DEFAULT &&
            signal != 17 && signal != 18 && signal != 23 && signal != POSIX_SIGWINCH)
            return signal;
    return 0;
}

void posix_kernel_signal_exec(posix_kernel *kernel) {
    kernel->aborting = 0;
    for (int signal = 1; signal <= POSIX_SIGNAL_MAX; signal++) {
        if (kernel->signal_disposition[signal] != POSIX_SIGNAL_HANDLER) continue;
        kernel->signal_disposition[signal] = POSIX_SIGNAL_DEFAULT;
        kernel->signal_handlers[signal] = POSIX_SIG_DFL;
        memset(&kernel->signal_action_masks[signal], 0, sizeof(posix_sigset));
        kernel->signal_action_flags[signal] = 0;
        if (signal == 17 || signal == 18 || signal == 23 || signal == POSIX_SIGWINCH)
            signal_bit_clear(&kernel->pending_signals, signal);
    }
    kernel->delivered_signal = 0;
}

void posix_kernel_cancel_wait(posix_kernel *kernel) {
    if (!kernel) return;
    if (kernel->lock_owner && kernel->lock_owner->waiting_kernel == kernel)
        kernel->lock_owner->waiting_kernel = NULL;
    posix_kernel_file_release(kernel->wait.lock_file);
    kernel->wait.lock_file = NULL;
    if (kernel->wait.has_signal_mask)
        kernel->signal_mask = kernel->wait.saved_mask;
    kernel->wait.active = 0;
    kernel->wait.read_minimum = 0;
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
        const posix_ofd *ofd = kernel->fds[fd].ofd;
        if (wait->read_minimum && ofd->kind == POSIX_OFD_TERMINAL &&
            !ofd->terminal.eof && ofd->terminal.input_length < wait->read_minimum)
            readiness &= ~POSIX_POLL_IN;
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
    posix_kernel_alarm_poll(kernel);
    route_terminal_signals(kernel);
    if (first_unmasked_pending(kernel)) return POSIX_WAIT_SIGNAL;
    if (kernel->wait.lock_file) {
        int fd = kernel->wait.lock_fd;
        posix_ofd *ofd = fd_valid(fd) ? kernel->fds[fd].ofd : NULL;
        if (!ofd || ofd->kind != POSIX_OFD_REGULAR ||
            ofd->regular.file != kernel->wait.lock_file) return POSIX_WAIT_READY;
        return record_conflict(kernel->wait.lock_file, kernel->lock_owner,
            kernel->wait.lock_type, kernel->wait.lock_start, kernel->wait.lock_end) ?
            POSIX_WAIT_BLOCKED : POSIX_WAIT_READY;
    }
    if (wait_sets_ready(kernel, &kernel->wait)) return POSIX_WAIT_READY;
    if (kernel->wait.has_deadline && kernel->clock_now) {
        uint64_t now = kernel->clock_now(kernel->clock_data);
        if (now >= kernel->wait.deadline_ns) return POSIX_WAIT_TIMEOUT;
    }
    return POSIX_WAIT_BLOCKED;
}

int posix_kernel_wait_until(posix_kernel *kernel, uint64_t deadline_ns) {
    if (!kernel || !kernel->clock_now) return -POSIX_ENOSYS;
    if (kernel->clock_now(kernel->clock_data) >= deadline_ns) {
        posix_kernel_cancel_wait(kernel);
        return 0;
    }
    if (kernel->wait.active && kernel->wait.has_deadline &&
        kernel->wait.deadline_ns == deadline_ns) return -POSIX_EAGAIN;
    posix_kernel_cancel_wait(kernel);
    posix_fd_zero(&kernel->wait.readfds);
    posix_fd_zero(&kernel->wait.writefds);
    posix_fd_zero(&kernel->wait.exceptfds);
    kernel->wait.nfds = 0;
    kernel->wait.has_deadline = 1;
    kernel->wait.deadline_ns = deadline_ns;
    kernel->wait.generation++;
    if (kernel->wait.generation == 0) kernel->wait.generation = 1;
    kernel->wait.active = 1;
    return -POSIX_EAGAIN;
}

static int kernel_wait_read(posix_kernel *kernel, int fd, int minimum) {
    if (!kernel || !fd_valid(fd)) return -POSIX_EINVAL;
    int readiness = posix_kernel_query_readiness(kernel, fd);
    if (readiness < 0) return readiness;
    const posix_ofd *ofd = kernel->fds[fd].ofd;
    if (minimum && ofd->kind == POSIX_OFD_TERMINAL &&
        !ofd->terminal.eof && ofd->terminal.input_length < minimum)
        readiness &= ~POSIX_POLL_IN;
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
    kernel->wait.read_minimum = minimum;
    kernel->wait.has_deadline = 0;
    kernel->wait.has_signal_mask = 0;
    kernel->wait.generation++;
    if (kernel->wait.generation == 0) kernel->wait.generation = 1;
    kernel->wait.active = 1;
    return -POSIX_EAGAIN;
}

int posix_kernel_wait_read(posix_kernel *kernel, int fd) {
    return kernel_wait_read(kernel, fd, 0);
}

/* --- Readiness --- */

int posix_kernel_query_readiness(posix_kernel *kernel, int fd) {
    if (!kernel || !fd_valid(fd)) return -POSIX_EINVAL;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;

    int mask = 0;
    switch (ofd->kind) {
    case POSIX_OFD_STREAM:
        if (ofd->input_handle >= 0 &&
            (ofd->terminal.input_length || ofd->terminal.eof)) mask |= POSIX_POLL_IN;
        if (ofd->terminal.eof) mask |= POSIX_POLL_HUP;
        if (ofd->output_handle >= 0) mask |= POSIX_POLL_OUT;
        break;
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
        if (!ofd->stream_mode || ofd->output_handle >= 0)
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
    case POSIX_OFD_NULL:
        if (ofd->null_device.readable) mask |= POSIX_POLL_IN;
        if (ofd->null_device.writable) mask |= POSIX_POLL_OUT;
        break;
    }
    return mask;
}

/* --- Terminal operations --- */

int posix_kernel_terminal_enqueue(posix_kernel *kernel, int fd,
                                  const uint8_t *data, int length) {
    return posix_kernel_terminal_enqueue_echo(kernel, fd, data, length, NULL, NULL);
}

int posix_kernel_terminal_enqueue_echo(posix_kernel *kernel, int fd,
    const uint8_t *data, int length, posix_terminal_echo_fn echo, void *context) {
    if (!kernel || !fd_valid(fd)) return -POSIX_EINVAL;
    if (!data || length < 0) return -POSIX_EINVAL;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;
    if (ofd->kind != POSIX_OFD_TERMINAL && ofd->kind != POSIX_OFD_STREAM)
        return -POSIX_EINVAL;
    if (ofd->stream_mode && ofd->input_handle < 0) return -POSIX_EBADF;

    int avail = ofd->terminal.input_capacity - ofd->terminal.input_length;
    int echoing = echo && (ofd->terminal.termios.lflag & POSIX_TERMIOS_LFLAG_ECHO);
    for (int i = 0; i < length && avail > 0; i++) {
        uint8_t byte = data[i];
        if ((ofd->terminal.termios.lflag & POSIX_TERMIOS_LFLAG_ISIG) &&
            byte == ofd->terminal.termios.cc[POSIX_TERMIOS_VINTR]) {
            ofd->terminal.input_length = 0;
            if (kernel->has_controlling_terminal &&
                ofd->terminal.controlling_session_id == kernel->session_id &&
                ofd->terminal.foreground_pgid == kernel->process_group_id)
                posix_kernel_signal_raise(kernel, 2);
            else
                signal_bit_set(&ofd->terminal.pending_signals, 2);
            avail = ofd->terminal.input_capacity;
            if (echoing) echo(context, fd, (const uint8_t *)"^C\n", 3);
            continue;
        }
        if (ofd->terminal.termios.lflag & POSIX_TERMIOS_LFLAG_ICANON) {
            if (byte == '\r' && (ofd->terminal.termios.iflag & POSIX_TERMIOS_IFLAG_ICRNL))
                byte = '\n';
            if (byte == ofd->terminal.termios.cc[POSIX_TERMIOS_VERASE] || byte == 8) {
                if (ofd->terminal.input_length > 0 &&
                    ofd->terminal.input[ofd->terminal.input_length - 1] != '\n') {
                    ofd->terminal.input_length--;
                    if (echoing) echo(context, fd, (const uint8_t *)"\b \b", 3);
                }
                avail = ofd->terminal.input_capacity - ofd->terminal.input_length;
                continue;
            }
            if (byte == ofd->terminal.termios.cc[POSIX_TERMIOS_VKILL]) {
                while (ofd->terminal.input_length > 0 &&
                       ofd->terminal.input[ofd->terminal.input_length - 1] != '\n') {
                    ofd->terminal.input_length--;
                    if (echoing) echo(context, fd, (const uint8_t *)"\b \b", 3);
                }
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
        if (echoing) echo(context, fd, &byte, 1);
        avail--;
    }
    return 0;
}

int posix_kernel_terminal_signal_eof(posix_kernel *kernel, int fd) {
    if (!kernel || !fd_valid(fd)) return -POSIX_EINVAL;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;
    if (ofd->kind != POSIX_OFD_TERMINAL && ofd->kind != POSIX_OFD_STREAM)
        return -POSIX_EINVAL;
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
    posix_winsize *current = &kernel->fds[fd].ofd->terminal.winsize;
    if (current->rows == winsize->rows &&
        current->columns == winsize->columns &&
        current->xpixels == winsize->xpixels &&
        current->ypixels == winsize->ypixels)
        return 0;
    *current = *winsize;
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
    if (!wofd) {
        p->readers = p->writers = 0;
        ofd_release(rofd);
        return -POSIX_ENOMEM;
    }
    wofd->pipe = p;
    wofd->status->flags = POSIX_O_WRONLY;

    kernel->fds[rfd].ofd = rofd;
    kernel->fds[rfd].cloexec = 0;
    kernel->fds[wfd].ofd = wofd;
    kernel->fds[wfd].cloexec = 0;
    fds[0] = rfd;
    fds[1] = wfd;
    return 0;
}

/* --- Descriptor operations --- */

int posix_kernel_close(posix_kernel *kernel, int fd) {
    if (!kernel || !fd_valid(fd)) return -POSIX_EINVAL;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;
    if (ofd->kind == POSIX_OFD_REGULAR)
        record_release_file(kernel->lock_owner, ofd->regular.file);
    kernel->fds[fd].ofd = NULL;
    pipe_count_dec(ofd);
    ofd_release(ofd);
    return 0;
}

static int kernel_open_validate_flags(int flags) {
    if (flags & ~(POSIX_O_WRONLY | POSIX_O_RDWR | POSIX_O_CREAT | POSIX_O_EXCL |
                  POSIX_O_TRUNC | POSIX_O_APPEND | POSIX_O_NONBLOCK |
                  POSIX_O_DIRECTORY | POSIX_O_NOCTTY | POSIX_O_NOFOLLOW |
                  POSIX_O_CLOEXEC)) return -POSIX_EINVAL;
    if ((flags & POSIX_O_WRONLY) && (flags & POSIX_O_RDWR))
        return -POSIX_EINVAL;
    return 0;
}

/* Backing objects such as shared memory already own a namespace node;
 * reuse descriptor ownership without treating their private names as paths. */
static int kernel_open_node(posix_kernel *kernel, posix_kernel_path_node *node,
                             int flags) {
    int result = kernel_open_validate_flags(flags);
    if (result < 0) return result;
    if (!node) return -POSIX_ENOENT;
    if ((flags & POSIX_O_DIRECTORY) && node->metadata.kind != POSIX_NODE_DIRECTORY)
        return -POSIX_ENOTDIR;
    if (node->metadata.kind == POSIX_NODE_DIRECTORY) {
        if (flags & (POSIX_O_WRONLY | POSIX_O_RDWR | POSIX_O_CREAT |
                     POSIX_O_TRUNC | POSIX_O_APPEND)) return -POSIX_EISDIR;
        int directory_fd = kernel_find_free_fd(kernel, 0);
        if (directory_fd < 0) return directory_fd;
        posix_ofd *directory = ofd_alloc(POSIX_OFD_DIRECTORY);
        if (!directory) return -POSIX_ENOMEM;
        directory->directory.node = node;
        directory->directory.index = 0;
        directory->status->flags = flags & (POSIX_O_DIRECTORY | POSIX_O_NONBLOCK);
        kernel->fds[directory_fd].ofd = directory;
        kernel->fds[directory_fd].cloexec = !!(flags & POSIX_O_CLOEXEC);
        return directory_fd;
    }
    if (node->metadata.kind == POSIX_NODE_NULL) {
        int fd = kernel_find_free_fd(kernel, 0);
        if (fd < 0) return fd;
        posix_ofd *ofd = ofd_alloc(POSIX_OFD_NULL);
        if (!ofd) return -POSIX_ENOMEM;
        ofd->null_device.file = node->file;
        posix_file_object_retain(node->file);
        ofd->null_device.readable = !(flags & POSIX_O_WRONLY);
        ofd->null_device.writable = !!(flags & (POSIX_O_WRONLY | POSIX_O_RDWR));
        ofd->status->flags = flags & (POSIX_O_WRONLY | POSIX_O_RDWR |
                                    POSIX_O_APPEND | POSIX_O_NONBLOCK);
        kernel->fds[fd].ofd = ofd;
        kernel->fds[fd].cloexec = !!(flags & POSIX_O_CLOEXEC);
        return fd;
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
    ofd->status->flags = flags & (POSIX_O_WRONLY | POSIX_O_RDWR |
                                POSIX_O_APPEND | POSIX_O_NONBLOCK);
    if ((flags & POSIX_O_TRUNC) && ofd->regular.writable) {
        free(ofd->regular.file->data); ofd->regular.file->data = NULL;
        ofd->regular.file->data_capacity = 0;
        node->file->metadata.size = 0;
        metadata_set_now(kernel, &node->file->metadata);
    }
    kernel->fds[fd].ofd = ofd;
    kernel->fds[fd].cloexec = !!(flags & POSIX_O_CLOEXEC);
    return fd;
}

static int kernel_open_at(posix_kernel *kernel, int directory, const uint8_t *path,
                           size_t length, int flags, int mode) {
    if (!kernel || !path || length == 0 || length >= POSIX_PATH_MAX)
        return -POSIX_EINVAL;
    int result = kernel_open_validate_flags(flags);
    if (result < 0) return result;
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    result = path_resolve_at(kernel, directory, path, length, path[length - 1] == '/',
                             !!(flags & POSIX_O_CREAT), normalized);
    if (result < 0) return result;
    posix_kernel_path_node *node = path_find(kernel, normalized);
    if (node && (flags & POSIX_O_CREAT) && (flags & POSIX_O_EXCL))
        return -POSIX_EEXIST;
    if (node && node->metadata.kind == POSIX_NODE_SYMLINK &&
        (flags & POSIX_O_NOFOLLOW)) return -POSIX_ELOOP;
    if (node && node->metadata.kind == POSIX_NODE_SYMLINK) {
        result = path_resolve_at(kernel, directory, path, length, 1,
                                  !!(flags & POSIX_O_CREAT), normalized);
        if (result < 0) return result;
        node = path_find(kernel, normalized);
    }
    if (!node && path[length - 1] == '/') return -POSIX_EISDIR;
    if ((flags & POSIX_O_DIRECTORY) &&
        (!node || node->metadata.kind != POSIX_NODE_DIRECTORY))
        return node ? -POSIX_ENOTDIR : -POSIX_ENOENT;
    if (!node) {
        if (path_prefix_is_file(kernel, normalized)) return -POSIX_ENOTDIR;
        if (!(flags & POSIX_O_CREAT)) return -POSIX_ENOENT;
        posix_path_metadata metadata = { POSIX_NODE_REGULAR,
            ((uint32_t)mode & 07777u) & ~kernel->creation_mask, 0, 0, 0,
            posix_kernel_allocate_inode(kernel), 0, 0 };
        metadata_set_now(kernel, &metadata);
        result = posix_kernel_path_add_data(kernel, normalized, &metadata, NULL, 0);
        if (result < 0) return result;
        node = path_find(kernel, normalized);
    }
    return kernel_open_node(kernel, node, flags);
}

int posix_kernel_open(posix_kernel *kernel, const uint8_t *path,
                       size_t length, int flags, int mode) {
    return kernel_open_at(kernel, POSIX_AT_FDCWD, path, length, flags, mode);
}

int posix_kernel_openat(posix_kernel *kernel, int directory,
                        const uint8_t *path, size_t length, int flags, int mode) {
    if (!length) return -POSIX_ENOENT;
    return kernel_open_at(kernel, directory, path, length, flags, mode);
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
        if (kernel->shm_namespace->count >= POSIX_SHM_OBJECT_MAX)
            return -POSIX_ENOMEM;
        object = &kernel->shm_namespace->objects[kernel->shm_namespace->count++];
        memcpy(object->name, name, length);
        object->name[length] = 0;
        posix_path_metadata metadata = { POSIX_NODE_REGULAR,
            ((uint32_t)(mode ? mode : 0666) & 0777u) & ~kernel->creation_mask,
            kernel->uid, kernel->gid,
            0, posix_kernel_allocate_inode(kernel), 0, 0 };
        metadata_set_now(kernel, &metadata);
        object->file = file_object_create(NULL, 0, &metadata);
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
        posix_path_metadata metadata = object->file->metadata;
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
    /* Exclusivity was checked against the shared-memory namespace above;
     * the backing path has already been materialized for this object. */
    return kernel_open_node(kernel, node, flags & ~POSIX_O_EXCL);
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
    if (!ofd->directory.node) return -POSIX_ENOENT;
    const char *parent = ofd->directory.node->path;
    size_t parent_length = strlen(parent);
    if (ofd->directory.index < 2) {
        const char *synthetic_name = ofd->directory.index == 0 ? "." : "..";
        posix_kernel_path_node *synthetic_node = ofd->directory.node;
        if (ofd->directory.index == 1 && parent_length > 1) {
            char parent_path[POSIX_PATH_NODE_NAME_MAX];
            memcpy(parent_path, parent, parent_length + 1);
            char *separator = parent_path + parent_length;
            while (separator > parent_path && *separator != '/') separator--;
            if (separator == parent_path) parent_path[1] = '\0';
            else *separator = '\0';
            synthetic_node = path_find(kernel, parent_path);
            if (!synthetic_node) return -POSIX_ENOENT;
        }
        if (capacity < strlen(synthetic_name) + 1) return -POSIX_ERANGE;
        memcpy(name, synthetic_name, strlen(synthetic_name) + 1);
        *metadata = synthetic_node->metadata;
        ofd->directory.index++;
        return 1;
    }
    for (int i = ofd->directory.index - 2;
         i < kernel->path_node_count; i++) {
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
        if (name_length + 1 > capacity) return -POSIX_ERANGE;
        memcpy(name, candidate + start, name_length + 1);
        *metadata = *node_metadata(&kernel->path_nodes[i]);
        ofd->directory.index = i + 3;
        return 1;
    }
    ofd->directory.index = kernel->path_node_count + 2;
    return 0;
}

int posix_kernel_lseek(posix_kernel *kernel, int fd, int64_t offset,
                       int whence, int64_t *result) {
    if (!kernel || !result || !fd_valid(fd)) return -POSIX_EINVAL;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (ofd && ofd->kind == POSIX_OFD_NULL) {
        if (whence < 0 || whence > 2) return -POSIX_EINVAL;
        *result = 0;
        return 0;
    }
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
    if (!ofd->directory.node) return -POSIX_ENOENT;
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

int posix_kernel_record_lock(posix_kernel *kernel, int fd, int command,
                              posix_flock *lock) {
    if (!kernel || !lock) {
        if (kernel && command == POSIX_F_SETLKW) posix_kernel_cancel_wait(kernel);
        return -POSIX_EFAULT;
    }
    if (!fd_valid(fd) || !kernel->fds[fd].ofd) {
        if (command == POSIX_F_SETLKW) posix_kernel_cancel_wait(kernel);
        return -POSIX_EBADF;
    }
    if (command != POSIX_F_GETLK && command != POSIX_F_SETLK &&
        command != POSIX_F_SETLKW) return -POSIX_EINVAL;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (ofd->kind != POSIX_OFD_REGULAR) {
        if (command == POSIX_F_SETLKW) posix_kernel_cancel_wait(kernel);
        return -POSIX_EINVAL;
    }
    posix_lock_owner *owner = kernel->lock_owner;
    if (!owner) return -POSIX_ENOSYS;
    posix_file_object *file = ofd->regular.file;
    int type = lock->type;
    uint64_t start, end;
    if (command == POSIX_F_SETLKW && kernel->wait.active && kernel->wait.lock_file) {
        if (kernel->wait.lock_fd != fd || kernel->wait.lock_file != file) {
            posix_kernel_cancel_wait(kernel);
            return -POSIX_EBADF;
        }
        type = kernel->wait.lock_type;
        start = kernel->wait.lock_start;
        end = kernel->wait.lock_end;
    } else {
        if (type != POSIX_F_RDLCK && type != POSIX_F_WRLCK && type != POSIX_F_UNLCK)
            return -POSIX_EINVAL;
        if (command == POSIX_F_GETLK && type == POSIX_F_UNLCK) return -POSIX_EINVAL;
        if (lock->whence < 0 || lock->whence > 2) return -POSIX_EINVAL;
        if (lock->start < INT32_MIN || lock->start > INT32_MAX ||
            lock->length < INT32_MIN || lock->length > INT32_MAX) return -POSIX_EOVERFLOW;
        uint64_t base = lock->whence == 0 ? 0 : lock->whence == 1 ?
            ofd->regular.offset : file->data_capacity;
        if (base > INT64_MAX || (lock->start > 0 && base > (uint64_t)(INT64_MAX - lock->start)))
            return -POSIX_EOVERFLOW;
        int64_t position = (int64_t)base + lock->start;
        if (position < 0) return -POSIX_EINVAL;
        if (position > INT32_MAX) return -POSIX_EOVERFLOW;
        if (lock->length < 0) {
            if (position + lock->length < 0) return -POSIX_EINVAL;
            start = (uint64_t)(position + lock->length);
            end = (uint64_t)position - 1;
        } else {
            start = (uint64_t)position;
            end = lock->length ? start + (uint64_t)lock->length - 1 : UINT64_MAX;
            if (lock->length && end > INT32_MAX) return -POSIX_EOVERFLOW;
        }
    }
    if (command != POSIX_F_GETLK &&
        ((type == POSIX_F_RDLCK && !ofd->regular.readable) ||
         (type == POSIX_F_WRLCK && !ofd->regular.writable))) return -POSIX_EBADF;
    if (command == POSIX_F_SETLKW && posix_kernel_signal_interrupt(kernel)) {
        /* The guest adapter can restart after a handler changes the fd offset.
         * Preserve the already resolved range in its private retry operand;
         * SETLKW never copies this operand back into the guest struct flock. */
        lock->type = type;
        lock->whence = 0;
        lock->start = (int64_t)start;
        lock->length = end == UINT64_MAX ? 0 : (int64_t)(end - start + 1);
        return -POSIX_EINTR;
    }
    posix_record_lock *conflict = type == POSIX_F_UNLCK ? NULL :
        record_conflict(file, owner, type, start, end);
    if (command == POSIX_F_GETLK) {
        if (!conflict) { lock->type = POSIX_F_UNLCK; return 0; }
        uint64_t length = conflict->end == UINT64_MAX ? 0 : conflict->end - conflict->start + 1;
        if (conflict->start > INT32_MAX || length > INT32_MAX) return -POSIX_EOVERFLOW;
        lock->type = conflict->type;
        lock->whence = 0;
        lock->start = (int64_t)conflict->start;
        lock->length = (int64_t)length;
        lock->pid = conflict->owner->pid;
        return 0;
    }
    if (!conflict) {
        if (command == POSIX_F_SETLKW) posix_kernel_cancel_wait(kernel);
        return record_replace(file, owner, type, start, end);
    }
    if (command == POSIX_F_SETLK) return -POSIX_EAGAIN;
    posix_lock_owner *visited[POSIX_RECORD_LOCK_MAX];
    unsigned count = 0;
    for (posix_record_lock *record = file->locks; record; record = record->file_next) {
        if (record->owner == owner || !record_overlap(start, end, record) ||
            (type != POSIX_F_WRLCK && record->type != POSIX_F_WRLCK)) continue;
        int cycle = record_wait_cycle(record->owner, owner, visited, &count);
        if (cycle) {
            posix_kernel_cancel_wait(kernel);
            return cycle < 0 ? cycle : -POSIX_EDEADLK;
        }
    }
    if (!kernel->wait.active) {
        if (file->refs == UINT32_MAX) return -POSIX_ENOLCK;
        posix_wait_record *wait = &kernel->wait;
        posix_fd_zero(&wait->readfds);
        posix_fd_zero(&wait->writefds);
        posix_fd_zero(&wait->exceptfds);
        wait->nfds = wait->has_deadline = wait->has_signal_mask = 0;
        wait->lock_file = file;
        posix_file_object_retain(file);
        wait->lock_fd = fd;
        wait->lock_type = type;
        wait->lock_start = start;
        wait->lock_end = end;
        if (++wait->generation == 0) wait->generation = 1;
        wait->active = 1;
        owner->waiting_kernel = kernel;
    } else if (!kernel->wait.lock_file) return -POSIX_EBUSY;
    return -POSIX_EAGAIN;
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

int posix_kernel_get_status_flags(const posix_kernel *kernel, int fd) {
    if (!kernel || !fd_valid(fd) || !kernel->fds[fd].ofd) return -POSIX_EBADF;
    return kernel->fds[fd].ofd->status->flags;
}

int posix_kernel_set_status_flags(posix_kernel *kernel, int fd, int flags) {
    if (!kernel || !fd_valid(fd) || !kernel->fds[fd].ofd) return -POSIX_EBADF;
    /* Access mode, creation flags and descriptor flags cannot be changed by
     * F_SETFL. dup/fork share these mutable flags through the same OFD. */
    const int mutable = POSIX_O_APPEND | POSIX_O_NONBLOCK;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    ofd->status->flags = (ofd->status->flags & ~mutable) | (flags & mutable);
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
    case POSIX_OFD_STREAM: {
        if (ofd->input_handle < 0) return -POSIX_EBADF;
        int n = ofd->terminal.input_length;
        if (!n) {
            if (ofd->terminal.eof) return 0;
            if (ofd->status->flags & POSIX_O_NONBLOCK) {
                posix_kernel_cancel_wait(kernel);
                return -POSIX_EAGAIN;
            }
            if (posix_kernel_signal_interrupt(kernel)) return -POSIX_EINTR;
            posix_kernel_wait_read(kernel, fd);
            return -POSIX_EAGAIN;
        }
        if (n > count) n = count;
        memcpy(buf, ofd->terminal.input, (size_t)n);
        ofd->terminal.input_length -= n;
        memmove(ofd->terminal.input, ofd->terminal.input + n,
                (size_t)ofd->terminal.input_length);
        posix_kernel_cancel_wait(kernel);
        return n;
    }
    case POSIX_OFD_TERMINAL: {
        if (ofd->stream_mode && ofd->input_handle < 0) return -POSIX_EBADF;
        if (ofd->terminal.input_length == 0) {
            if (ofd->terminal.eof) return 0;
            if (ofd->status->flags & POSIX_O_NONBLOCK) {
                posix_kernel_cancel_wait(kernel);
                return -POSIX_EAGAIN;
            }
            if (!(ofd->terminal.termios.lflag & POSIX_TERMIOS_LFLAG_ICANON) &&
                ofd->terminal.termios.cc[POSIX_TERMIOS_VMIN] == 0 &&
                ofd->terminal.termios.cc[POSIX_TERMIOS_VTIME] == 0)
                return 0;
            if (posix_kernel_signal_interrupt(kernel)) return -POSIX_EINTR;
            terminal_wait_with_timeout(kernel, fd,
                ofd->terminal.termios.cc[POSIX_TERMIOS_VTIME], count);
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
                if (ofd->status->flags & POSIX_O_NONBLOCK) {
                    posix_kernel_cancel_wait(kernel);
                    return -POSIX_EAGAIN;
                }
                if (posix_kernel_signal_interrupt(kernel)) return -POSIX_EINTR;
                posix_kernel_wait_read(kernel, fd);
                return -POSIX_EAGAIN;
            }
        } else {
            uint8_t vmin = ofd->terminal.termios.cc[POSIX_TERMIOS_VMIN];
            uint8_t vtime = ofd->terminal.termios.cc[POSIX_TERMIOS_VTIME];
            int minimum = vmin < count ? vmin : count;
            if (minimum > 0 && available < minimum && !ofd->terminal.eof &&
                !(ofd->status->flags & POSIX_O_NONBLOCK)) {
                if (posix_kernel_signal_interrupt(kernel)) return -POSIX_EINTR;
                terminal_wait_with_timeout(kernel, fd, vtime, count);
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
            if (ofd->status->flags & POSIX_O_NONBLOCK) {
                posix_kernel_cancel_wait(kernel);
                return -POSIX_EAGAIN;
            }
            if (posix_kernel_signal_interrupt(kernel)) return -POSIX_EINTR;
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
    case POSIX_OFD_NULL:
        return ofd->null_device.readable ? 0 : -POSIX_EBADF;
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

int posix_kernel_fsync(posix_kernel *kernel, int fd) {
    if (!kernel || !fd_valid(fd) || !kernel->fds[fd].ofd)
        return -POSIX_EBADF;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (ofd->kind != POSIX_OFD_REGULAR && ofd->kind != POSIX_OFD_DIRECTORY)
        return -POSIX_EINVAL;
    /* Writes and namespace updates commit synchronously to this in-memory
     * mount. A future persistent mount must provide its own flush operation. */
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
    ofd->regular.file->metadata.size = (int64_t)size;
    metadata_set_now(kernel, &ofd->regular.file->metadata);
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
    object->metadata.size = (int64_t)object->data_capacity;
    if (count) {
        memcpy(object->data + (size_t)offset, buf, count);
        metadata_set_now(kernel, &object->metadata);
    }
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
    int result = posix_kernel_file_write_object(
        kernel, ofd->regular.file, offset, buf, count);
    return result;
}

int posix_kernel_write(posix_kernel *kernel, int fd,
                       const void *buf, int count) {
    if (!kernel || !fd_valid(fd)) return -POSIX_EINVAL;
    if (!buf || count < 0) return -POSIX_EINVAL;
    if (count == 0) return 0;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;

    switch (ofd->kind) {
    case POSIX_OFD_STREAM:
        return ofd->output_handle >= 0 ? count : -POSIX_EBADF;
    case POSIX_OFD_NULL:
        return ofd->null_device.writable ? count : -POSIX_EBADF;
    case POSIX_OFD_TERMINAL:
        if (ofd->stream_mode && ofd->output_handle < 0) return -POSIX_EBADF;
        /* Terminal output always succeeds (browser consumes it). */
        return count;
    case POSIX_OFD_PIPE_WRITE: {
        posix_pipe *p = ofd->pipe;
        if (p->readers == 0) return -POSIX_EPIPE;
        int avail = p->capacity - p->length;
        /* A nonblocking write that fits in one pipe buffer must either fit
         * completely or leave the buffer untouched. Larger writes may be short. */
        if (avail == 0 || ((ofd->status->flags & POSIX_O_NONBLOCK) &&
                          count <= p->capacity && count > avail)) {
            if (ofd->status->flags & POSIX_O_NONBLOCK) {
                posix_kernel_cancel_wait(kernel);
                return -POSIX_EAGAIN;
            }
            if (posix_kernel_signal_interrupt(kernel)) return -POSIX_EINTR;
            posix_fd_set writers, errors;
            posix_fd_zero(&writers);
            posix_fd_zero(&errors);
            posix_fd_set_bit(fd, &writers);
            posix_fd_set_bit(fd, &errors);
            (void)posix_kernel_select(kernel, fd + 1, NULL, &writers, &errors, NULL);
            return -POSIX_EAGAIN;
        }
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
        size_t offset = (ofd->status->flags & POSIX_O_APPEND) ? ofd->regular.file->data_capacity :
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
        ofd->regular.file->metadata.size = (int64_t)ofd->regular.file->data_capacity;
        metadata_set_now(kernel, &ofd->regular.file->metadata);
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
    kernel->wait.read_minimum = 0;
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
