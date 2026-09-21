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
    if (ofd->kind == POSIX_OFD_PIPE_READ) ofd->pipe->readers--;
    else if (ofd->kind == POSIX_OFD_PIPE_WRITE) ofd->pipe->writers--;
}

static void ofd_release(posix_ofd *ofd) {
    if (!ofd || --ofd->ref_count > 0) return;
    switch (ofd->kind) {
    case POSIX_OFD_TERMINAL:
        free(ofd->terminal.input);
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

static int kernel_find_free_fd(posix_kernel *k, int from) {
    for (int i = from; i < POSIX_KERNEL_FD_MAX; i++)
        if (!k->fds[i].ofd) return i;
    return -POSIX_EMFILE;
}

static int fd_valid(int fd) {
    return fd >= 0 && fd < POSIX_KERNEL_FD_MAX;
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
    if (!kernel || !path || !metadata ||
        posix_path_metadata_validate(metadata) < 0) return -POSIX_EINVAL;
    size_t length = bounded_length(path);
    if (length == 0 || length >= POSIX_PATH_MAX) return -POSIX_EINVAL;
    char normalized[POSIX_PATH_NODE_NAME_MAX];
    int result = path_normalize(kernel, (const uint8_t *)path, length, normalized);
    if (result < 0) return result;
    posix_kernel_path_node *existing = path_find(kernel, normalized);
    if (existing) { existing->metadata = *metadata; return 0; }
    if (kernel->path_node_count >= POSIX_PATH_NODE_MAX) return -POSIX_ENOMEM;
    existing = &kernel->path_nodes[kernel->path_node_count++];
    memcpy(existing->path, normalized, strlen(normalized) + 1);
    existing->metadata = *metadata;
    return 0;
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
    *metadata = node->metadata;
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

/* --- Lifecycle --- */

posix_kernel *posix_kernel_create(int interactive) {
    posix_kernel *k = calloc(1, sizeof(posix_kernel));
    if (!k) return NULL;

    memcpy(k->cwd, "/", 2);
    const posix_path_metadata root = { POSIX_NODE_DIRECTORY, 0755, 0, 0, 0, 1 };
    const posix_path_metadata directory = { POSIX_NODE_DIRECTORY, 0755, 0, 0, 0, 2 };
    if (posix_kernel_path_add(k, "/", &root) < 0 ||
        posix_kernel_path_add(k, "/bin", &directory) < 0 ||
        posix_kernel_path_add(k, "/usr", &directory) < 0 ||
        posix_kernel_path_add(k, "/usr/bin", &directory) < 0) {
        free(k);
        return NULL;
    }

    if (interactive) {
        posix_ofd *tty = ofd_alloc(POSIX_OFD_TERMINAL);
        if (!tty) { free(k); return NULL; }
        tty->terminal.input = malloc(POSIX_TERMINAL_INPUT_CAPACITY);
        if (!tty->terminal.input) { free(tty); free(k); return NULL; }
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
        /* fds 0, 1, 2 share the same terminal OFD */
        tty->ref_count = 3;
        k->fds[0].ofd = tty;
        k->fds[1].ofd = tty;
        k->fds[2].ofd = tty;
    }
    return k;
}

posix_kernel *posix_kernel_clone(const posix_kernel *source) {
    if (!source) return NULL;
    posix_kernel *clone = calloc(1, sizeof(*clone));
    if (!clone) return NULL;
    memcpy(clone, source, sizeof(*clone));
    memset(clone->fds, 0, sizeof(clone->fds));
    clone->wait.active = 0;
    for (int fd = 0; fd < POSIX_KERNEL_FD_MAX; fd++) {
        const posix_ofd *source_ofd = source->fds[fd].ofd;
        if (!source_ofd) continue;
        posix_ofd *shared = (posix_ofd *)source_ofd;
        clone->fds[fd].ofd = shared;
        shared->ref_count++;
        if (shared->kind == POSIX_OFD_PIPE_READ) shared->pipe->readers++;
        if (shared->kind == POSIX_OFD_PIPE_WRITE) shared->pipe->writers++;
    }
    return clone;
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
    free(kernel);
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
            posix_kernel_signal_raise(kernel, 2);
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
                if (ofd->terminal.input_length == 0) ofd->terminal.eof = 1;
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
    return newfd;
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
    return newfd;
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
            posix_kernel_wait_read(kernel, fd);
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
        } else if (ofd->terminal.termios.cc[POSIX_TERMIOS_VMIN] > 0 &&
                   available < ofd->terminal.termios.cc[POSIX_TERMIOS_VMIN] &&
                   !ofd->terminal.eof) {
            posix_kernel_wait_read(kernel, fd);
            return -POSIX_EAGAIN;
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
    case POSIX_OFD_PIPE_WRITE:
        return -POSIX_EBADF;
    }
    return -POSIX_EINVAL;
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
            if (signal) signal_bit_clear(&kernel->pending_signals, signal);
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
