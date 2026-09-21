#ifndef WASTE_POSIX_KERNEL_H
#define WASTE_POSIX_KERNEL_H

#include "select.h"
#include "path.h"

#include <stdint.h>
#include <stddef.h>

/* --- Constants --- */

#define POSIX_KERNEL_FD_MAX      64
#define POSIX_PIPE_CAPACITY    4096
#define POSIX_TERMINAL_INPUT_CAPACITY 4096
#define POSIX_TERMIOS_CC_COUNT 20

/* Stable wasm32 terminal ABI.  The layout intentionally uses fixed-width
 * fields instead of the host libc's termios definition. */
#define POSIX_TERMIOS_IFLAG_ICRNL 0x0001u
#define POSIX_TERMIOS_OFLAG_OPOST 0x0001u
#define POSIX_TERMIOS_OFLAG_ONLCR 0x0002u
#define POSIX_TERMIOS_LFLAG_ISIG  0x0001u
#define POSIX_TERMIOS_LFLAG_ICANON 0x0002u
#define POSIX_TERMIOS_LFLAG_ECHO  0x0008u
#define POSIX_TERMIOS_LFLAG_IEXTEN 0x8000u
#define POSIX_TERMIOS_VINTR  0
#define POSIX_TERMIOS_VEOF   4
#define POSIX_TERMIOS_VERASE 2
#define POSIX_TERMIOS_VKILL  3
#define POSIX_TERMIOS_VMIN   6
#define POSIX_TERMIOS_VTIME  5
#define POSIX_TCIFLUSH 0
#define POSIX_TCOFLUSH 1
#define POSIX_TCIOFLUSH 2
#define POSIX_TCOOFF 0
#define POSIX_TCOON 1
#define POSIX_TCIOFF 2
#define POSIX_TCION 3
#define POSIX_SIGWINCH 28
#define POSIX_SIGKILL 9
#define POSIX_SIGSTOP 19
#define POSIX_SIG_DFL UINT32_C(0)
#define POSIX_SIG_IGN UINT32_C(0xfffffffe)

typedef enum {
    POSIX_SIGNAL_DEFAULT = 0,
    POSIX_SIGNAL_IGNORE = 1,
    POSIX_SIGNAL_HANDLER = 2
} posix_signal_disposition;

typedef struct {
    uint32_t iflag;
    uint32_t oflag;
    uint32_t cflag;
    uint32_t lflag;
    uint8_t cc[POSIX_TERMIOS_CC_COUNT];
} posix_termios;

typedef struct {
    uint16_t rows;
    uint16_t columns;
    uint16_t xpixels;
    uint16_t ypixels;
} posix_winsize;

/* Readiness mask bits */
#define POSIX_POLL_IN   0x01   /* readable data available */
#define POSIX_POLL_OUT  0x02   /* writable without blocking */
#define POSIX_POLL_ERR  0x04   /* error condition (e.g. broken pipe) */
#define POSIX_POLL_HUP  0x08   /* hangup / EOF */

/* Errno values (POSIX, independent of host libc) */
#define POSIX_EBADF    9
#define POSIX_E2BIG    7
#define POSIX_ENOEXEC  8
#define POSIX_EINTR    4
#define POSIX_ENOMEM  12
#define POSIX_EAGAIN  11
#define POSIX_EINVAL  22
#define POSIX_EMFILE  24
#define POSIX_EPIPE   32
#define POSIX_ECHILD  10
#define POSIX_EFAULT  14
#define POSIX_EEXIST  17
#define POSIX_EBUSY   16
#define POSIX_ENOSYS  38
#define POSIX_WNOHANG 1
#define POSIX_WUNTRACED 2
#define POSIX_WCONTINUED 8
#define POSIX_TIOCGWINSZ 0x5413u
#define POSIX_TIOCSWINSZ 0x5414u
#define POSIX_F_GETFD 1
#define POSIX_F_SETFD 2
#define POSIX_FD_CLOEXEC 1
#define POSIX_O_WRONLY 1
#define POSIX_O_RDWR 2
#define POSIX_O_CREAT 64
#define POSIX_O_TRUNC 512
#define POSIX_O_APPEND 1024

#define POSIX_WAIT_BLOCKED 0
#define POSIX_WAIT_READY   1
#define POSIX_WAIT_TIMEOUT 2
#define POSIX_WAIT_SIGNAL  3
#define POSIX_SIGNAL_MAX   (POSIX_SIGSET_BYTES * 8)

/* --- Types --- */

typedef enum {
    POSIX_OFD_TERMINAL,
    POSIX_OFD_PIPE_READ,
    POSIX_OFD_PIPE_WRITE,
    POSIX_OFD_REGULAR,
    POSIX_OFD_DIRECTORY,
} posix_ofd_kind;

typedef struct posix_kernel_path_node posix_kernel_path_node;

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
            posix_termios termios;
            posix_winsize winsize;
            int foreground_pgid;
            posix_sigset pending_signals;
        } terminal;
        struct {
            posix_kernel_path_node *node;
            size_t offset;
            int readable;
            int writable;
            int append;
        } regular;
        struct {
            posix_kernel_path_node *node;
            int index;
        } directory;
        posix_pipe *pipe;
    };
} posix_ofd;

/* Per-fd table entry. */
typedef struct {
    posix_ofd *ofd;    /* NULL = closed */
    uint8_t cloexec;
} posix_fd_entry;

struct posix_kernel_path_node {
    char path[POSIX_PATH_NODE_NAME_MAX];
    posix_path_metadata metadata;
    uint8_t *data;
    size_t data_capacity;
    char *link_target;
};

typedef uint64_t (*posix_clock_now_fn)(void *data);

/* A wait contains copied interests only; it never retains guest pointers. */
typedef struct {
    int active;
    uint64_t generation;
    int nfds;
    posix_fd_set readfds;
    posix_fd_set writefds;
    posix_fd_set exceptfds;
    int has_deadline;
    uint64_t deadline_ns;
    int has_signal_mask;
    posix_sigset saved_mask;
    posix_sigset temporary_mask;
} posix_wait_record;

/* Per-sandbox kernel. */
typedef struct posix_kernel {
    posix_fd_entry fds[POSIX_KERNEL_FD_MAX];
    posix_wait_record wait;
    posix_clock_now_fn clock_now;
    void *clock_data;
    posix_sigset signal_mask;
    posix_sigset pending_signals;
    /* Signal consumed by the most recent interrupted wait, if any. */
    int delivered_signal;
    int process_group_id;
    uint8_t signal_disposition[POSIX_SIGSET_BYTES * 8 + 1];
    uint32_t signal_handlers[POSIX_SIGSET_BYTES * 8 + 1];
    posix_sigset signal_action_masks[POSIX_SIGSET_BYTES * 8 + 1];
    char cwd[POSIX_PATH_NODE_NAME_MAX];
    posix_kernel_path_node path_nodes[POSIX_PATH_NODE_MAX];
    int path_node_count;
} posix_kernel;

/* --- Lifecycle --- */

/* Create a kernel.  interactive=1 opens fds 0,1,2 as a shared terminal;
   interactive=0 leaves all fds closed (for WAST test sandboxes).
   Returns NULL on allocation failure. */
posix_kernel *posix_kernel_create(int interactive);

/* Clone a process kernel.  Descriptor entries are copied while preserving
   shared open-file descriptions and pipe endpoint identity. */
posix_kernel *posix_kernel_clone(const posix_kernel *kernel);

/* Destroy a kernel and all owned resources. */
void posix_kernel_destroy(posix_kernel *kernel);

/* Install the monotonic clock used for finite wait deadlines. */
void posix_kernel_set_clock(posix_kernel *kernel, posix_clock_now_fn clock_now,
                            void *clock_data);

/* Poll/cancel the pointer-free wait record. */
int posix_kernel_wait_poll(posix_kernel *kernel);
int posix_kernel_wait_active(const posix_kernel *kernel);
uint64_t posix_kernel_wait_generation(const posix_kernel *kernel);
void posix_kernel_cancel_wait(posix_kernel *kernel);

/* Register a descriptor-read wait without retaining guest pointers.  Returns
   zero when readable now, or -POSIX_EAGAIN after registration. */
int posix_kernel_wait_read(posix_kernel *kernel, int fd);

/* Per-sandbox signal state.  Signal numbers are one-based POSIX numbers. */
void posix_kernel_set_signal_mask(posix_kernel *kernel,
                                  const posix_sigset *mask);
void posix_kernel_get_signal_mask(const posix_kernel *kernel,
                                  posix_sigset *mask);
int posix_kernel_signal_raise(posix_kernel *kernel, int signal);
int posix_kernel_signal_clear(posix_kernel *kernel, int signal);
int posix_kernel_signal_pending(const posix_kernel *kernel, int signal);
int posix_kernel_signal_last_delivered(posix_kernel *kernel);
int posix_kernel_signal_set_disposition(posix_kernel *kernel, int signal,
                                        posix_signal_disposition disposition);
int posix_kernel_signal_get_disposition(const posix_kernel *kernel, int signal,
                                        posix_signal_disposition *disposition);
int posix_kernel_signal_set_handler(posix_kernel *kernel, int signal,
                                    uint32_t handler);
int posix_kernel_signal_get_handler(const posix_kernel *kernel, int signal,
                                    uint32_t *handler);
int posix_kernel_signal_set_action_mask(posix_kernel *kernel, int signal,
                                         const posix_sigset *mask);
int posix_kernel_signal_get_action_mask(const posix_kernel *kernel, int signal,
                                        posix_sigset *mask);
int posix_kernel_signal_enter_handler(posix_kernel *kernel, int signal,
                                      posix_sigset *saved_mask);
void posix_kernel_signal_leave_handler(posix_kernel *kernel,
                                       const posix_sigset *saved_mask);
int posix_kernel_getpgid(const posix_kernel *kernel);
int posix_kernel_setpgid(posix_kernel *kernel, int pgid);
int posix_kernel_terminal_get_foreground_pgid(const posix_kernel *kernel,
                                              int fd);
int posix_kernel_terminal_set_foreground_pgid(posix_kernel *kernel, int fd,
                                              int pgid);

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

/* Apply terminal output flags to a bounded byte span. */
int posix_kernel_terminal_process_output(const posix_kernel *kernel, int fd,
                                         const uint8_t *input, int length,
                                         uint8_t *output, int capacity);
int posix_kernel_isatty(const posix_kernel *kernel, int fd);
int posix_kernel_tcgetattr(posix_kernel *kernel, int fd,
                           posix_termios *termios);
int posix_kernel_tcsetattr(posix_kernel *kernel, int fd,
                           const posix_termios *termios);
int posix_kernel_tcflow(posix_kernel *kernel, int fd, int action);
int posix_kernel_terminal_get_winsize(const posix_kernel *kernel, int fd,
                                      posix_winsize *winsize);
int posix_kernel_terminal_set_winsize(posix_kernel *kernel, int fd,
                                      const posix_winsize *winsize);

/* --- Pipe --- */

/* Create a pipe.  fds[0] = read end, fds[1] = write end.
   Returns 0 on success or negative errno. */
int posix_kernel_pipe(posix_kernel *kernel, int fds[2]);

/* --- Descriptor operations --- */

/* Close a descriptor.  Returns 0 on success or negative errno. */
int posix_kernel_close(posix_kernel *kernel, int fd);
int posix_kernel_open(posix_kernel *kernel, const uint8_t *path, size_t length,
                      int flags, int mode);
int posix_kernel_readdir(posix_kernel *kernel, int fd, char *name,
                         size_t capacity, posix_path_metadata *metadata);
int posix_kernel_lseek(posix_kernel *kernel, int fd, int64_t offset,
                       int whence, int64_t *result);

/* Duplicate a descriptor to the lowest available fd.
   Returns the new fd or negative errno. */
int posix_kernel_dup(posix_kernel *kernel, int oldfd);

/* Duplicate oldfd to exactly newfd.  If newfd is open, it is closed first.
   Returns newfd on success or negative errno. */
int posix_kernel_dup2(posix_kernel *kernel, int oldfd, int newfd);
int posix_kernel_get_cloexec(const posix_kernel *kernel, int fd);
int posix_kernel_set_cloexec(posix_kernel *kernel, int fd, int enabled);
void posix_kernel_close_on_exec(posix_kernel *kernel);

/* --- I/O (non-blocking) --- */

/* Read up to count bytes from fd.  Returns bytes read (> 0), 0 at EOF,
   or negative errno (-POSIX_EBADF, -POSIX_EAGAIN). */
int posix_kernel_read(posix_kernel *kernel, int fd, void *buf, int count);

/* Write up to count bytes to fd.  Returns bytes written (> 0) or
   negative errno (-POSIX_EBADF, -POSIX_EAGAIN, -POSIX_EPIPE). */
int posix_kernel_write(posix_kernel *kernel, int fd,
                       const void *buf, int count);

/* --- Select/pselect --- */

/* Synchronous select.  fd sets are in/out: on input they contain the
   interest bits; on output only the ready bits remain.
   timeout may be NULL (indefinite wait) or point to {0,0} (poll).
   Returns the total ready-bit count across all output sets,
   0 when nothing is ready and the timeout is zero,
   -POSIX_EAGAIN when a wait was registered for the scheduler, or negative
   errno on error
   (-POSIX_EINVAL, -POSIX_EBADF). */
int posix_kernel_select(posix_kernel *kernel, int nfds,
                        posix_fd_set *readfds, posix_fd_set *writefds,
                        posix_fd_set *exceptfds, const posix_timeval *timeout);

/* pselect.  Same wait semantics as select but with a timespec timeout (const
   per POSIX) and an optional temporary signal mask. */
int posix_kernel_pselect(posix_kernel *kernel, int nfds,
                         posix_fd_set *readfds, posix_fd_set *writefds,
                         posix_fd_set *exceptfds,
                         const posix_timespec *timeout,
                         const posix_sigset *sigmask);

#endif /* WASTE_POSIX_KERNEL_H */
