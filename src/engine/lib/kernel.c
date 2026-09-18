#include "include/kernel.h"

#include <stdlib.h>
#include <string.h>

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

/* --- Lifecycle --- */

posix_kernel *posix_kernel_create(int interactive) {
    posix_kernel *k = calloc(1, sizeof(posix_kernel));
    if (!k) return NULL;

    if (interactive) {
        posix_ofd *tty = ofd_alloc(POSIX_OFD_TERMINAL);
        if (!tty) { free(k); return NULL; }
        tty->terminal.input = malloc(POSIX_TERMINAL_INPUT_CAPACITY);
        if (!tty->terminal.input) { free(tty); free(k); return NULL; }
        tty->terminal.input_length = 0;
        tty->terminal.input_capacity = POSIX_TERMINAL_INPUT_CAPACITY;
        tty->terminal.eof = 0;
        /* fds 0, 1, 2 share the same terminal OFD */
        tty->ref_count = 3;
        k->fds[0].ofd = tty;
        k->fds[1].ofd = tty;
        k->fds[2].ofd = tty;
    }
    return k;
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

/* --- Readiness --- */

int posix_kernel_query_readiness(posix_kernel *kernel, int fd) {
    if (!kernel || !fd_valid(fd)) return -POSIX_EINVAL;
    posix_ofd *ofd = kernel->fds[fd].ofd;
    if (!ofd) return -POSIX_EBADF;

    int mask = 0;
    switch (ofd->kind) {
    case POSIX_OFD_TERMINAL:
        if (ofd->terminal.input_length > 0 || ofd->terminal.eof)
            mask |= POSIX_POLL_IN;
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
    if (length > avail) length = avail;
    if (length > 0) {
        memcpy(ofd->terminal.input + ofd->terminal.input_length, data, length);
        ofd->terminal.input_length += length;
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
        if (ofd->terminal.input_length == 0)
            return ofd->terminal.eof ? 0 : -POSIX_EAGAIN;
        int n = count < ofd->terminal.input_length
                    ? count : ofd->terminal.input_length;
        memcpy(buf, ofd->terminal.input, n);
        ofd->terminal.input_length -= n;
        if (ofd->terminal.input_length > 0)
            memmove(ofd->terminal.input, ofd->terminal.input + n,
                    ofd->terminal.input_length);
        return n;
    }
    case POSIX_OFD_PIPE_READ: {
        posix_pipe *p = ofd->pipe;
        if (p->length == 0)
            return p->writers == 0 ? 0 : -POSIX_EAGAIN;
        int n = count < p->length ? count : p->length;
        memcpy(buf, p->buffer, n);
        p->length -= n;
        if (p->length > 0)
            memmove(p->buffer, p->buffer + n, p->length);
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
        return n;
    }
    case POSIX_OFD_PIPE_READ:
        return -POSIX_EBADF;
    }
    return -POSIX_EINVAL;
}
