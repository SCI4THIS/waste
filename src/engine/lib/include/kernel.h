#ifndef WASTE_POSIX_KERNEL_H
#define WASTE_POSIX_KERNEL_H

#include <stdint.h>
#include <stddef.h>

/* --- Constants --- */

#define POSIX_KERNEL_FD_MAX      64
#define POSIX_PIPE_CAPACITY    4096
#define POSIX_TERMINAL_INPUT_CAPACITY 4096

/* Readiness mask bits */
#define POSIX_POLL_IN   0x01   /* readable data available */
#define POSIX_POLL_OUT  0x02   /* writable without blocking */
#define POSIX_POLL_ERR  0x04   /* error condition (e.g. broken pipe) */
#define POSIX_POLL_HUP  0x08   /* hangup / EOF */

/* Errno values (POSIX, independent of host libc) */
#define POSIX_EBADF    9
#define POSIX_ENOMEM  12
#define POSIX_EAGAIN  11
#define POSIX_EINVAL  22
#define POSIX_EMFILE  24
#define POSIX_EPIPE   32

/* --- Types --- */

typedef enum {
    POSIX_OFD_TERMINAL,
    POSIX_OFD_PIPE_READ,
    POSIX_OFD_PIPE_WRITE,
} posix_ofd_kind;

/* Shared pipe buffer between read and write endpoints. */
typedef struct posix_pipe {
    uint8_t *buffer;
    int length;        /* bytes available to read */
    int capacity;
    int readers;       /* number of read-end OFDs alive */
    int writers;       /* number of write-end OFDs alive */
} posix_pipe;

/* Open file description — shared across dup'd descriptors. */
typedef struct posix_ofd {
    posix_ofd_kind kind;
    int ref_count;     /* number of fd entries pointing here */
    union {
        struct {
            uint8_t *input;
            int input_length;
            int input_capacity;
            int eof;
        } terminal;
        posix_pipe *pipe;
    };
} posix_ofd;

/* Per-fd table entry. */
typedef struct {
    posix_ofd *ofd;    /* NULL = closed */
} posix_fd_entry;

/* Per-sandbox kernel. */
typedef struct posix_kernel {
    posix_fd_entry fds[POSIX_KERNEL_FD_MAX];
} posix_kernel;

/* --- Lifecycle --- */

/* Create a kernel.  interactive=1 opens fds 0,1,2 as a shared terminal;
   interactive=0 leaves all fds closed (for WAST test sandboxes).
   Returns NULL on allocation failure. */
posix_kernel *posix_kernel_create(int interactive);

/* Destroy a kernel and all owned resources. */
void posix_kernel_destroy(posix_kernel *kernel);

/* --- Readiness --- */

/* Query readiness for a descriptor.  Returns a POSIX_POLL_* mask (>= 0)
   or a negative errno (-POSIX_EBADF, -POSIX_EINVAL). */
int posix_kernel_query_readiness(posix_kernel *kernel, int fd);

/* --- Terminal operations --- */

/* Enqueue input bytes into the terminal associated with fd.
   Returns 0 on success or negative errno. */
int posix_kernel_terminal_enqueue(posix_kernel *kernel, int fd,
                                  const uint8_t *data, int length);

/* Signal EOF on the terminal associated with fd.
   Returns 0 on success or negative errno. */
int posix_kernel_terminal_signal_eof(posix_kernel *kernel, int fd);

/* --- Pipe --- */

/* Create a pipe.  fds[0] = read end, fds[1] = write end.
   Returns 0 on success or negative errno. */
int posix_kernel_pipe(posix_kernel *kernel, int fds[2]);

/* --- Descriptor operations --- */

/* Close a descriptor.  Returns 0 on success or negative errno. */
int posix_kernel_close(posix_kernel *kernel, int fd);

/* Duplicate a descriptor to the lowest available fd.
   Returns the new fd or negative errno. */
int posix_kernel_dup(posix_kernel *kernel, int oldfd);

/* Duplicate oldfd to exactly newfd.  If newfd is open, it is closed first.
   Returns newfd on success or negative errno. */
int posix_kernel_dup2(posix_kernel *kernel, int oldfd, int newfd);

/* --- I/O (non-blocking) --- */

/* Read up to count bytes from fd.  Returns bytes read (> 0), 0 at EOF,
   or negative errno (-POSIX_EBADF, -POSIX_EAGAIN). */
int posix_kernel_read(posix_kernel *kernel, int fd, void *buf, int count);

/* Write up to count bytes to fd.  Returns bytes written (> 0) or
   negative errno (-POSIX_EBADF, -POSIX_EAGAIN, -POSIX_EPIPE). */
int posix_kernel_write(posix_kernel *kernel, int fd,
                       const void *buf, int count);

#endif /* WASTE_POSIX_KERNEL_H */
