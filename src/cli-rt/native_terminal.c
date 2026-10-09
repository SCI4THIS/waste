#define _POSIX_C_SOURCE 200809L
#include "native_terminal.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted, resized;
static native_terminal *owner;
static const int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGWINCH, SIGPIPE};
static void terminal_event(int signal) {
    if (signal == SIGWINCH) resized = 1;
    else interrupted |= (sig_atomic_t)1 << signal;
}
int native_terminal_signal(void) {
    for (unsigned i = 0; i < sizeof(signals) / sizeof(signals[0]); i++)
        if (interrupted & ((sig_atomic_t)1 << signals[i])) return signals[i];
    return 0;
}
int native_terminal_take_signal(void) {
    sigset_t blocked, saved;
    sigemptyset(&blocked);
    for (unsigned i = 0; i < sizeof(signals) / sizeof(signals[0]); i++)
        sigaddset(&blocked, signals[i]);
    sigprocmask(SIG_BLOCK, &blocked, &saved);
    int signal = native_terminal_signal();
    if (signal) interrupted &= ~((sig_atomic_t)1 << signal);
    sigprocmask(SIG_SETMASK, &saved, NULL);
    return signal;
}
int native_terminal_resized(void) {
    /* Clear before querying ioctl: a later signal leaves another update queued. */
    if (!resized) return 0;
    resized = 0;
    return 1;
}
static int same_terminal(const native_terminal *t, int a, int b) {
    return t->tty[a] && t->tty[b] &&
        t->metadata[a].st_dev == t->metadata[b].st_dev &&
        t->metadata[a].st_ino == t->metadata[b].st_ino &&
        t->metadata[a].st_rdev == t->metadata[b].st_rdev;
}
int native_terminal_resize(native_terminal *t, posix_kernel *kernel) {
    int updated = 0;
    for (int fd = 0; fd < POSIX_KERNEL_FD_MAX; fd++) {
        posix_ofd *ofd = kernel->fds[fd].ofd;
        if (!ofd || ofd->kind != POSIX_OFD_TERMINAL) continue;
        int handle = ofd->input_handle >= 0 ? ofd->input_handle : ofd->output_handle;
        if (handle < 0 || handle >= 3 || t->handles[handle] < 0) continue;
        struct winsize size;
        if (ioctl(t->handles[handle], TIOCGWINSZ, &size)) return -1;
        if (!size.ws_row || !size.ws_col) continue;
        posix_winsize dimensions = {size.ws_row, size.ws_col, size.ws_xpixel, size.ws_ypixel};
        if (posix_kernel_terminal_set_winsize(kernel, fd, &dimensions)) return -1;
        updated = 1;
    }
    return updated;
}
int native_terminal_open(native_terminal *t, posix_kernel *kernel) {
    memset(t, 0, sizeof(*t));
    for (int i = 0; i < 3; i++) t->handles[i] = -1;
    if (owner) return -1;
    owner = t;
    t->enabled = 1;
    interrupted = resized = 0;
    struct sigaction action = {0};
    sigemptyset(&action.sa_mask);
    for (unsigned i = 0; i < sizeof(signals) / sizeof(signals[0]); i++)
        sigaddset(&action.sa_mask, signals[i]);
    action.sa_handler = terminal_event;
    for (unsigned i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        action.sa_handler = signals[i] == SIGPIPE ? SIG_IGN : terminal_event;
        if (sigaction(signals[i], &action, &t->previous[i])) return -1;
        t->signal_count++;
    }
    for (int i = 0; i < 3; i++) {
        int flags = fcntl(i, F_GETFL);
        if (flags < 0 && errno == EBADF) continue;
        if (flags < 0 || (t->handles[i] = fcntl(i, F_DUPFD_CLOEXEC, 3)) < 0 ||
            fstat(t->handles[i], &t->metadata[i])) return -1;
        t->tty[i] = isatty(t->handles[i]);
        int input = (flags & O_ACCMODE) != O_WRONLY ? i : -1;
        int output = (flags & O_ACCMODE) != O_RDONLY ? i : -1;
        int peer = -1;
        for (int j = 0; j < i; j++) {
            if (same_terminal(t, i, j) &&
                (fcntl(t->handles[j], F_GETFL) & O_ACCMODE) == (flags & O_ACCMODE)) {
                peer = j;
                break;
            }
        }
        if (peer >= 0) {
            posix_ofd *ofd = kernel->fds[peer].ofd;
            if (input >= 0 && ofd->input_handle < 0) {
                /* Queue allocation remains kernel-owned. A write-only terminal
                 * is kept separate when a later capability can read it. */
                peer = -1;
            } else {
                if (output >= 0 && ofd->output_handle < 0) ofd->output_handle = output;
                if (posix_kernel_dup2(kernel, peer, i) < 0) return -1;
            }
        }
        if (peer < 0 && posix_kernel_attach_stream(kernel, i, t->tty[i],
                input, output, (uint32_t)(t->metadata[i].st_mode & 0177777u))) return -1;
        if (!t->tty[i]) continue;
        if (tcgetattr(t->handles[i], &t->saved[i])) return -1;
        int changed = 0;
        for (int j = 0; j < i; j++) {
            if (same_terminal(t, i, j) && t->changed[j]) {
                t->saved[i] = t->saved[j];
                changed = 1;
                break;
            }
        }
        posix_termios *guest = &kernel->fds[i].ofd->terminal.termios;
        guest->lflag = (t->saved[i].c_lflag & ISIG) ? POSIX_TERMIOS_LFLAG_ISIG : 0;
        if (t->saved[i].c_lflag & ICANON) guest->lflag |= POSIX_TERMIOS_LFLAG_ICANON;
        if (t->saved[i].c_lflag & ECHO) guest->lflag |= POSIX_TERMIOS_LFLAG_ECHO;
        if (t->saved[i].c_iflag & ICRNL) guest->iflag |= POSIX_TERMIOS_IFLAG_ICRNL;
        guest->oflag = (t->saved[i].c_oflag & OPOST) ? POSIX_TERMIOS_OFLAG_OPOST : 0;
        if (t->saved[i].c_oflag & ONLCR) guest->oflag |= POSIX_TERMIOS_OFLAG_ONLCR;
        guest->cc[POSIX_TERMIOS_VINTR] = t->saved[i].c_cc[VINTR];
        guest->cc[POSIX_TERMIOS_VEOF] = t->saved[i].c_cc[VEOF];
        guest->cc[POSIX_TERMIOS_VERASE] = t->saved[i].c_cc[VERASE];
        guest->cc[POSIX_TERMIOS_VKILL] = t->saved[i].c_cc[VKILL];
        if (changed) continue;
        struct termios raw = t->saved[i];
        raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
        raw.c_oflag &= ~OPOST;
        raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
        raw.c_cflag = (raw.c_cflag & ~(CSIZE | PARENB)) | CS8;
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(t->handles[i], TCSANOW, &raw)) return -1;
        t->changed[i] = 1;
    }
    return native_terminal_resize(t, kernel) < 0 ? -1 : 0;
}
int native_terminal_close(native_terminal *t) {
    if (!t->enabled) return 0;
    int failed = 0;
    for (int i = 0; i < 3; i++) {
        if (t->changed[i] && tcsetattr(t->handles[i], TCSANOW, &t->saved[i])) failed = 1;
        if (t->handles[i] >= 0) close(t->handles[i]);
        t->handles[i] = -1;
    }
    for (int i = t->signal_count - 1; i >= 0; i--)
        if (sigaction(signals[i], &t->previous[i], NULL)) failed = 1;
    if (owner == t) owner = NULL;
    t->enabled = 0;
    return failed ? -1 : 0;
}
