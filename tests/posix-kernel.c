/* posix-kernel.c — Native unit tests for the per-sandbox POSIX kernel:
 * lifecycle, descriptor table, terminal and pipe readiness, dup/close,
 * read/write, and cross-kernel isolation.
 * Retained CHECK boundaries: docs/posix-kernel-retained-coverage.md.
 * Built with ASan/UBSan, linked against system libc. */

#include "lib/include/kernel.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;
static int tests = 0;
static uint64_t test_clock_now(void *data);

#define CHECK(cond, ...) do { \
    tests++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
        failures++; \
    } \
} while (0)

/* --- Lifecycle --- */

static void test_lifecycle(void) {
    /* Noninteractive: all fds closed */
    posix_kernel *k = posix_kernel_create(0);
    CHECK(k != NULL, "create noninteractive");
    for (int i = 0; i < POSIX_KERNEL_FD_MAX; i++)
        CHECK(posix_kernel_query_readiness(k, i) == -POSIX_EBADF,
              "noninteractive fd %d should be closed", i);
    posix_kernel_destroy(k);

    /* Interactive: fds 0,1,2 are terminals, rest closed */
    k = posix_kernel_create(1);
    CHECK(k != NULL, "create interactive");
    for (int i = 0; i < 3; i++) {
        int r = posix_kernel_query_readiness(k, i);
        CHECK(r >= 0, "interactive fd %d should be open", i);
    }
    for (int i = 3; i < POSIX_KERNEL_FD_MAX; i++)
        CHECK(posix_kernel_query_readiness(k, i) == -POSIX_EBADF,
              "interactive fd %d should be closed", i);
    posix_kernel_destroy(k);

    /* NULL kernel destroy is safe */
    posix_kernel_destroy(NULL);
}

/* --- Invalid descriptor queries --- */

static void test_invalid_fd(void) {
    posix_kernel *k = posix_kernel_create(0);

    CHECK(posix_kernel_query_readiness(k, -1) == -POSIX_EINVAL, "fd -1");
    CHECK(posix_kernel_query_readiness(k, POSIX_KERNEL_FD_MAX) == -POSIX_EINVAL,
          "fd == FD_MAX");
    CHECK(posix_kernel_query_readiness(k, POSIX_KERNEL_FD_MAX + 100) == -POSIX_EINVAL,
          "fd >> FD_MAX");
    CHECK(posix_kernel_query_readiness(k, 0) == -POSIX_EBADF,
          "closed fd 0 in noninteractive");
    CHECK(posix_kernel_query_readiness(NULL, 0) == -POSIX_EINVAL,
          "NULL kernel");

    posix_kernel_destroy(k);

    /* Interactive: fd 5 is closed */
    k = posix_kernel_create(1);
    CHECK(posix_kernel_query_readiness(k, 5) == -POSIX_EBADF,
          "closed fd 5 in interactive");
    posix_kernel_destroy(k);
}

/* --- File-backed mapping source --- */

static void test_file_read_at(void) {
    posix_kernel *k = posix_kernel_create(0);
    const uint8_t source[] = "mapped-bytes";
    uint8_t result[6] = {0};
    posix_path_metadata metadata = {
        POSIX_NODE_REGULAR, 0666, 0, 0, sizeof(source) - 1, 77, 0, 0
    };
    CHECK(k != NULL, "create file mapping kernel");
    CHECK(posix_kernel_path_add_data(k, "/mapped", &metadata, source,
                                     sizeof(source) - 1) == 0,
          "add file mapping source");
    int fd = posix_kernel_open(k, (const uint8_t *)"/mapped", 7, 0, 0);
    CHECK(fd >= 0, "open file mapping source");
    CHECK(posix_kernel_file_read_at(k, fd, 2, result, sizeof(result)) == 6 &&
          memcmp(result, "pped-b", sizeof(result)) == 0,
          "read mapping bytes without changing descriptor offset");
    memset(result, 0, sizeof(result));
    CHECK(posix_kernel_read(k, fd, result, sizeof(result)) == 6 &&
          memcmp(result, source, sizeof(result)) == 0,
          "descriptor offset remains independent");
    int writable_fd = posix_kernel_open(k, (const uint8_t *)"/mapped", 7,
                                        POSIX_O_RDWR, 0);
    CHECK(writable_fd >= 0 &&
          posix_kernel_file_write_at(k, writable_fd, 2, "XY", 2) == 2 &&
          posix_kernel_file_read_at(k, fd, 2, result, sizeof(result)) == 6 &&
          memcmp(result, "XYed-b", sizeof(result)) == 0,
          "write mapping bytes without changing descriptor offset");
    uint64_t object_id = 0;
    int writable = 0;
    posix_file_object *object = NULL;
    CHECK(posix_kernel_file_identity(k, writable_fd, &object_id,
                                     &writable) == 0 &&
          object_id == metadata.inode && writable &&
          posix_kernel_file_retain(k, writable_fd, &object, &writable) == 0 &&
          object != NULL,
          "capture writable file object identity");
    CHECK(posix_kernel_close(k, writable_fd) == 0 &&
          posix_kernel_file_write_object(k, object, 1, "Q", 1) == 1 &&
          posix_kernel_file_read_at(k, fd, 0, result, sizeof(result)) == 6 &&
          memcmp(result, "mQXYed", sizeof(result)) == 0,
          "file object write survives descriptor close");
    posix_kernel_file_release(object);
    posix_kernel *child_kernel = posix_kernel_clone(k);
    int child_fd = child_kernel ? posix_kernel_open(
        child_kernel, (const uint8_t *)"/mapped", 7, POSIX_O_RDWR, 0) : -1;
    CHECK(child_fd >= 0 &&
          posix_kernel_file_write_at(child_kernel, child_fd, 0, "Z", 1) == 1 &&
          posix_kernel_file_read_at(k, fd, 0, result, sizeof(result)) == 6 &&
          memcmp(result, "ZQXYed", sizeof(result)) == 0,
          "forked kernels share retained VFS file objects");
    posix_kernel_destroy(child_kernel);
    CHECK(posix_kernel_file_read_at(k, fd, 8, result, 8) == -POSIX_EINVAL,
          "reject file mapping range past EOF");
    CHECK(posix_kernel_file_read_at(k, fd, 0, result, 1) == 1 &&
          result[0] == 'Z', "read shared mapping source after fork write");
    int truncate_fd = posix_kernel_open(k, (const uint8_t *)"/mapped", 7,
                                        POSIX_O_RDWR, 0);
    uint64_t truncated_size = 0;
    CHECK(truncate_fd >= 0 &&
          posix_kernel_ftruncate(k, truncate_fd, 4) == 0 &&
          posix_kernel_file_size(k, truncate_fd, &truncated_size) == 0 &&
          truncated_size == 4 &&
          posix_kernel_file_read_at(k, truncate_fd, 4, result, 1) == -POSIX_EINVAL,
          "truncate shared file object and reject its old tail");
    CHECK(posix_kernel_ftruncate(k, truncate_fd, 8) == 0 &&
          posix_kernel_file_size(k, truncate_fd, &truncated_size) == 0 &&
          truncated_size == 8,
          "grow truncated file object");
    posix_kernel_close(k, truncate_fd);
    posix_kernel_destroy(k);
}

static void test_fork_path_publication(void) {
    posix_kernel *parent = posix_kernel_create(0);
    posix_kernel *child = posix_kernel_clone(parent);
    uint8_t bytes[4] = {0};
    int child_fd = child ? posix_kernel_open(
        child, (const uint8_t *)"/fork-output", 12,
        POSIX_O_CREAT | POSIX_O_RDWR, 0644) : -1;
    CHECK(parent && child && child_fd >= 0,
          "create fork child output file");
    CHECK(child_fd >= 0 && posix_kernel_write(child, child_fd, "data", 4) == 4,
          "write fork child output file");
    CHECK(posix_kernel_path_access(parent, (const uint8_t *)"/fork-output",
                                   12, POSIX_F_OK, 0) == -POSIX_ENOENT,
          "fork child path remains unpublished before reap");
    CHECK(posix_kernel_merge_paths(parent, child) == 0 &&
          posix_kernel_path_access(parent, (const uint8_t *)"/fork-output",
                                   12, POSIX_F_OK, 0) == 0,
          "publish fork child pathname changes at reap");
    int parent_fd = posix_kernel_open(parent,
        (const uint8_t *)"/fork-output", 12, 0, 0);
    CHECK(parent_fd >= 0 && posix_kernel_read(parent, parent_fd, bytes, 4) == 4 &&
          memcmp(bytes, "data", 4) == 0,
          "published fork output retains shared bytes");
    posix_kernel_destroy(child);
    posix_kernel_destroy(parent);
}

/* Guest names, capacity errors, iteration EOF and close checks now run in
 * directory-umask.wast. Retain exact host-seeded inode metadata here. */
static void test_directory_metadata(void) {
    posix_kernel *k = posix_kernel_create(0);
    const posix_path_metadata tmp = {
        POSIX_NODE_DIRECTORY, 0755, 0, 0, 0, 40, 0, 0
    };
    const posix_path_metadata child = {
        POSIX_NODE_DIRECTORY, 0750, 0, 0, 0, 41, 0, 0
    };
    const posix_path_metadata file = {
        POSIX_NODE_REGULAR, 0644, 0, 0, 0, 42, 0, 0
    };
    char name[32];
    posix_path_metadata metadata;
    CHECK(k && posix_kernel_path_add_data(k, "/tmp", &tmp, NULL, 0) == 0 &&
          posix_kernel_path_add_data(k, "/tmp/child", &child, NULL, 0) == 0 &&
          posix_kernel_path_add_data(k, "/tmp/child/file", &file, NULL, 0) == 0,
          "create directory iteration fixture");
    int fd = posix_kernel_open(k, (const uint8_t *)"/tmp/child", 10, 0, 0);
    CHECK(posix_kernel_readdir(k, fd, name, sizeof(name), &metadata) == 1 &&
          metadata.inode == child.inode,
          "directory dot preserves seeded child inode");
    CHECK(posix_kernel_readdir(k, fd, name, sizeof(name), &metadata) == 1 &&
          metadata.inode == tmp.inode,
          "directory parent dot preserves seeded parent inode");
    CHECK(posix_kernel_readdir(k, fd, name, sizeof(name), &metadata) == 1 &&
          metadata.inode == file.inode,
          "directory child preserves seeded file inode");
    posix_kernel_close(k, fd);

    fd = posix_kernel_open(k, (const uint8_t *)"/", 1, 0, 0);
    CHECK(fd >= 0 &&
          posix_kernel_readdir(k, fd, name, sizeof(name), &metadata) == 1 &&
          metadata.inode == 1,
          "root directory dot preserves fixed root inode");
    CHECK(posix_kernel_readdir(k, fd, name, sizeof(name), &metadata) == 1 &&
          metadata.inode == 1,
          "root directory parent preserves fixed root inode");
    posix_kernel_destroy(k);
}

static void test_creation_mask(void) {
    posix_kernel *k = posix_kernel_create(0);
    posix_path_metadata metadata;
    uint64_t now = UINT64_C(1700000000123456789);
    posix_kernel_set_realtime_clock(k, test_clock_now, &now);
    int fd = k ? posix_kernel_open(k, (const uint8_t *)"/default-mask", 13,
                                   POSIX_O_CREAT | POSIX_O_RDWR, 0666) : -1;
    CHECK(fd >= 0 && posix_kernel_path_stat(
              k, (const uint8_t *)"/default-mask", 13, 1, &metadata) == 0 &&
          metadata.mtime_sec == 1700000000 &&
          metadata.mtime_nsec == 123456789,
          "injected realtime timestamp applies to new file");
    now = UINT64_C(1700000001987654321);
    CHECK(posix_kernel_write(k, fd, "x", 1) == 1 &&
          posix_kernel_path_stat(k, (const uint8_t *)"/default-mask", 13,
                                 1, &metadata) == 0 &&
          metadata.mtime_sec == 1700000001 &&
          metadata.mtime_nsec == 987654321,
          "successful write refreshes modification time");
    /* Guest creation modes and umask returns live in directory-umask.wast. */
    posix_kernel_umask(k, 0077);
    posix_kernel *child = posix_kernel_clone(k);
    CHECK(child && posix_kernel_umask(child, 0002) == 0077 &&
          posix_kernel_umask(k, 0077) == 0077,
          "fork clone inherits an independent creation mask");
    posix_kernel_destroy(child);
    posix_kernel_destroy(k);
}

/* --- Terminal readiness --- */

static void test_terminal_readiness(void) {
    posix_kernel *k = posix_kernel_create(1);

    /* Initially: writable but not readable (no input) */
    int r = posix_kernel_query_readiness(k, 0);
    CHECK((r & POSIX_POLL_OUT) != 0, "terminal writable initially");
    CHECK((r & POSIX_POLL_IN) == 0, "terminal not readable initially");
    CHECK((r & POSIX_POLL_HUP) == 0, "terminal no hangup initially");

    /* fds 0,1,2 share the same OFD — same readiness */
    CHECK(posix_kernel_query_readiness(k, 1) == r, "fd 1 same as fd 0");
    CHECK(posix_kernel_query_readiness(k, 2) == r, "fd 2 same as fd 0");

    /* Enqueue input → readable */
    const uint8_t input[] = "hello";
    CHECK(posix_kernel_terminal_enqueue(k, 0, input, 5) == 0, "enqueue ok");
    r = posix_kernel_query_readiness(k, 0);
    CHECK((r & POSIX_POLL_IN) != 0, "terminal readable after enqueue");
    CHECK((r & POSIX_POLL_OUT) != 0, "terminal still writable");

    /* Read drains input → not readable */
    uint8_t buf[32];
    /* Guest read/EOF/write values live in guest-session-terminal-readiness.wast.
     * Keep real setup and all thirteen host/raw readiness checks. */
    posix_kernel_read(k, 0, buf, sizeof(buf));
    r = posix_kernel_query_readiness(k, 0);
    CHECK((r & POSIX_POLL_IN) == 0, "terminal not readable after drain");

    /* EOF → readable + hangup */
    CHECK(posix_kernel_terminal_signal_eof(k, 0) == 0, "signal eof ok");
    r = posix_kernel_query_readiness(k, 0);
    CHECK((r & POSIX_POLL_IN) != 0, "terminal readable at eof");
    CHECK((r & POSIX_POLL_HUP) != 0, "terminal hangup at eof");

    /* Read at EOF returns 0 */
    posix_kernel_read(k, 0, buf, sizeof(buf));

    /* Enqueue on non-terminal fails */
    CHECK(posix_kernel_terminal_enqueue(k, 5, input, 5) == -POSIX_EBADF,
          "enqueue on closed fd");

    /* Terminal write always succeeds */
    posix_kernel_write(k, 1, input, 5);

    posix_kernel_destroy(k);
}

static void test_terminal_modes(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_termios termios = {0};
    /* Guest attribute/read/edit/flow results moved to the shared terminal
     * readiness session. Keep setup/drains for raw masks and host APIs. */
    posix_kernel_tcgetattr(k, 0, &termios);
    termios.iflag = POSIX_TERMIOS_IFLAG_ICRNL;
    termios.lflag = POSIX_TERMIOS_LFLAG_ICANON | POSIX_TERMIOS_LFLAG_ISIG;
    termios.cc[POSIX_TERMIOS_VERASE] = 127;
    termios.cc[POSIX_TERMIOS_VKILL] = 21;
    termios.cc[POSIX_TERMIOS_VINTR] = 3;
    posix_kernel_tcsetattr(k, 0, &termios);

    const uint8_t partial[] = "ab";
    CHECK(posix_kernel_terminal_enqueue(k, 0, partial, 2) == 0,
          "enqueue canonical partial line");
    CHECK((posix_kernel_query_readiness(k, 0) & POSIX_POLL_IN) == 0,
          "partial canonical line is not readable");
    const uint8_t line[] = "c\n";
    CHECK(posix_kernel_terminal_enqueue(k, 0, line, 2) == 0,
          "enqueue canonical newline");
    CHECK((posix_kernel_query_readiness(k, 0) & POSIX_POLL_IN) != 0,
          "complete canonical line is readable");
    uint8_t buffer[16] = {0};
    posix_kernel_read(k, 0, buffer, sizeof(buffer));

    const uint8_t edited[] = "xy\bZ\n";
    posix_kernel_terminal_enqueue(k, 0, edited, sizeof(edited) - 1);
    posix_kernel_read(k, 0, buffer, sizeof(buffer));

    posix_kernel_terminal_enqueue(k, 0, (const uint8_t *)"q", 1);
    posix_kernel_terminal_enqueue(k, 0, (const uint8_t *)"\003", 1);
    CHECK(posix_kernel_signal_pending(k, 2), "intr raises SIGINT");
    CHECK((posix_kernel_query_readiness(k, 0) & POSIX_POLL_IN) == 0,
          "intr discards partial line");

    termios.lflag = 0;
    termios.cc[POSIX_TERMIOS_VMIN] = 3;
    termios.cc[POSIX_TERMIOS_VTIME] = 0;
    posix_kernel_tcsetattr(k, 0, &termios);
    posix_kernel_terminal_enqueue(k, 0, (const uint8_t *)"xy", 2);
    CHECK(posix_kernel_read(k, 0, buffer, sizeof(buffer)) == -POSIX_EAGAIN,
          "raw read waits for VMIN bytes");
    posix_kernel_terminal_enqueue(k, 0, (const uint8_t *)"z", 1);
    posix_kernel_read(k, 0, buffer, sizeof(buffer));
    posix_winsize unchanged = {24, 80, 0, 0};
    CHECK(posix_kernel_terminal_set_winsize(k, 0, &unchanged) == 0,
          "accept unchanged terminal window size");
    CHECK(!posix_kernel_signal_pending(k, POSIX_SIGWINCH),
          "unchanged window size does not raise SIGWINCH");
    posix_winsize winsize = {40, 100, 0, 0};
    CHECK(posix_kernel_terminal_set_winsize(k, 0, &winsize) == 0,
          "set terminal window size");
    CHECK(posix_kernel_signal_pending(k, POSIX_SIGWINCH),
          "resize raises SIGWINCH");
    posix_kernel_destroy(k);
}

/* Guest EOF counts/contents and output bytes moved to the shared terminal
 * timing session. Keep the output helper's transformed-buffer lengths: guest
 * write returns source bytes consumed, not this private API's output length. */
static void test_terminal_output_lengths(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_termios termios = {0};
    uint8_t output[16];
    const uint8_t lines[] = "a\nb";

    posix_kernel_tcgetattr(k, 0, &termios);
    termios.iflag = POSIX_TERMIOS_IFLAG_ICRNL;
    termios.oflag = POSIX_TERMIOS_OFLAG_OPOST | POSIX_TERMIOS_OFLAG_ONLCR;
    termios.lflag = POSIX_TERMIOS_LFLAG_ICANON;
    termios.cc[POSIX_TERMIOS_VEOF] = 4;
    posix_kernel_tcsetattr(k, 0, &termios);
    CHECK(posix_kernel_terminal_process_output(k, 1, lines, 3, output,
                                               sizeof(output)) == 4,
          "ONLCR expands LF");
    termios.oflag = 0;
    posix_kernel_tcsetattr(k, 0, &termios);
    CHECK(posix_kernel_terminal_process_output(k, 1, lines, 3, output,
                                               sizeof(output)) == 3,
          "raw output preserves length");
    posix_kernel_destroy(k);
}

static uint64_t test_clock_now(void *data) {
    return *(uint64_t *)data;
}

static void test_terminal_vtime(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_termios termios = {0};
    uint8_t buffer[16];
    uint64_t now = 0;
    posix_kernel_set_clock(k, test_clock_now, &now);
    /* Guest timed read/attribute results moved to terminal-timing.wast.
     * Retain direct EAGAIN, registration and fake-clock polling. */
    posix_kernel_tcgetattr(k, 0, &termios);
    termios.lflag = 0;
    termios.cc[POSIX_TERMIOS_VMIN] = 0;
    termios.cc[POSIX_TERMIOS_VTIME] = 2;
    posix_kernel_tcsetattr(k, 0, &termios);
    CHECK(posix_kernel_read(k, 0, buffer, sizeof(buffer)) == -POSIX_EAGAIN,
          "VMIN=0 VTIME read yields");
    CHECK(posix_kernel_wait_active(k), "VMIN=0 VTIME registers wait");
    now = 200000000;
    CHECK(posix_kernel_wait_poll(k) == POSIX_WAIT_TIMEOUT,
          "VMIN=0 VTIME expires");
    posix_kernel_read(k, 0, buffer, sizeof(buffer));

    termios.cc[POSIX_TERMIOS_VMIN] = 3;
    termios.cc[POSIX_TERMIOS_VTIME] = 2;
    posix_kernel_tcsetattr(k, 0, &termios);
    now = 300000000;
    CHECK(posix_kernel_read(k, 0, buffer, sizeof(buffer)) == -POSIX_EAGAIN,
          "VMIN/VTIME read yields");
    posix_kernel_terminal_enqueue(k, 0, (const uint8_t *)"x", 1);
    now = 500000000;
    posix_kernel_read(k, 0, buffer, sizeof(buffer));
    posix_kernel_destroy(k);
}

/* --- Pipe creation and readiness --- */

static void test_pipe_readiness(void) {
    posix_kernel *k = posix_kernel_create(0);
    int fds[2];

    /* Guest creation, endpoint bounds and byte I/O are asserted by
     * pipe-descriptors.wast. Keep real setup and raw readiness/EAGAIN. */
    posix_kernel_pipe(k, fds);

    /* Read end: not readable initially */
    int r = posix_kernel_query_readiness(k, fds[0]);
    CHECK((r & POSIX_POLL_IN) == 0, "pipe read end not readable initially");
    CHECK((r & POSIX_POLL_HUP) == 0, "pipe read end no hangup initially");

    /* Write end: writable */
    r = posix_kernel_query_readiness(k, fds[1]);
    CHECK((r & POSIX_POLL_OUT) != 0, "pipe write end writable initially");
    CHECK((r & POSIX_POLL_ERR) == 0, "pipe write end no error initially");

    /* Write data → read end becomes readable */
    const uint8_t data[] = "test";
    posix_kernel_write(k, fds[1], data, 4);

    r = posix_kernel_query_readiness(k, fds[0]);
    CHECK((r & POSIX_POLL_IN) != 0, "pipe read end readable after write");

    /* Read data back */
    uint8_t buf[32];
    posix_kernel_read(k, fds[0], buf, sizeof(buf));

    r = posix_kernel_query_readiness(k, fds[0]);
    CHECK((r & POSIX_POLL_IN) == 0, "pipe read end not readable after drain");

    /* Read on empty pipe with writers open → EAGAIN */
    int n = posix_kernel_read(k, fds[0], buf, sizeof(buf));
    CHECK(n == -POSIX_EAGAIN, "pipe read empty returns EAGAIN");

    posix_kernel_destroy(k);
}

/* --- Pipe EOF and broken pipe --- */

static void test_pipe_close_transitions(void) {
    posix_kernel *k = posix_kernel_create(0);
    int fds[2];

    /* Close write end → read end gets HANGUP */
    /* Guest creation, buffered drain, EOF and EPIPE results are asserted by
     * pipe-descriptors.wast. Keep setup and the four raw HUP/ERR/IN/OUT checks. */
    posix_kernel_pipe(k, fds);
    const uint8_t data[] = "abc";
    posix_kernel_write(k, fds[1], data, 3);
    posix_kernel_close(k, fds[1]);

    int r = posix_kernel_query_readiness(k, fds[0]);
    CHECK((r & POSIX_POLL_IN) != 0, "read end readable (data + eof)");
    CHECK((r & POSIX_POLL_HUP) != 0, "read end hangup after write close");

    /* Read remaining data, then EOF */
    uint8_t buf[32];
    posix_kernel_read(k, fds[0], buf, sizeof(buf));
    posix_kernel_read(k, fds[0], buf, sizeof(buf));

    posix_kernel_close(k, fds[0]);

    /* Close read end → write end gets broken pipe */
    posix_kernel_pipe(k, fds);
    posix_kernel_close(k, fds[0]);

    r = posix_kernel_query_readiness(k, fds[1]);
    CHECK((r & POSIX_POLL_ERR) != 0, "write end error after read close");
    CHECK((r & POSIX_POLL_OUT) == 0, "write end not writable after read close");

    posix_kernel_write(k, fds[1], data, 3);

    posix_kernel_close(k, fds[1]);
    posix_kernel_destroy(k);
}

/* --- Pipe full --- */

static void test_pipe_full(void) {
    posix_kernel *k = posix_kernel_create(0);
    int fds[2];
    /* Guest creation is asserted by pipe-descriptors.wast (ordinal 36).
     * Keep real setup, every partial fill and exact capacity/raw readiness. */
    posix_kernel_pipe(k, fds);

    /* Fill the pipe */
    uint8_t block[256];
    memset(block, 'x', sizeof(block));
    int total = 0;
    for (;;) {
        int n = posix_kernel_write(k, fds[1], block, sizeof(block));
        if (n == -POSIX_EAGAIN) break;
        CHECK(n > 0, "pipe write partial ok");
        total += n;
    }
    CHECK(total == POSIX_PIPE_CAPACITY, "pipe filled to capacity");

    int r = posix_kernel_query_readiness(k, fds[1]);
    CHECK((r & POSIX_POLL_OUT) == 0, "pipe write end not writable when full");

    /* Read some → writable again */
    uint8_t drain[64];
    posix_kernel_read(k, fds[0], drain, sizeof(drain));
    r = posix_kernel_query_readiness(k, fds[1]);
    CHECK((r & POSIX_POLL_OUT) != 0, "pipe writable after partial read");

    posix_kernel_destroy(k);
}

/* --- Dup --- */

static void test_dup(void) {
    posix_kernel *k = posix_kernel_create(1);

    /* Dup fd 0 → new fd shares same OFD */
    int newfd = posix_kernel_dup(k, 0);
    /* Guest dup/read values moved to guest-session-terminal-descriptors.wast.
     * Keep real setup and the five raw readiness/lifetime checks. */
    CHECK(posix_kernel_query_readiness(k, newfd) ==
          posix_kernel_query_readiness(k, 0), "dup'd fd same readiness");

    /* Enqueue on original → duped fd also readable */
    const uint8_t input[] = "hi";
    posix_kernel_terminal_enqueue(k, 0, input, 2);
    int r = posix_kernel_query_readiness(k, newfd);
    CHECK((r & POSIX_POLL_IN) != 0, "dup'd fd readable after enqueue on original");

    /* Read from dup drains shared buffer */
    uint8_t buf[32];
    posix_kernel_read(k, newfd, buf, sizeof(buf));
    r = posix_kernel_query_readiness(k, 0);
    CHECK((r & POSIX_POLL_IN) == 0, "original not readable after dup read");

    /* Close dup → original still works */
    posix_kernel_close(k, newfd);
    CHECK(posix_kernel_query_readiness(k, newfd) == -POSIX_EBADF,
          "dup'd fd closed");
    CHECK(posix_kernel_query_readiness(k, 0) >= 0,
          "original still open after dup close");

    /* Invalid-descriptor checks moved to descriptor-flags.wast. */

    posix_kernel_destroy(k);
}

/* Guest-visible dup2 checks now live in engine-regressions/pipe-descriptors.wast.
 * The close-on-exec gate below retains direct dup2/clone ownership coverage. */

/* --- Pipe dup affects reader/writer counts --- */

static void test_pipe_dup_readiness(void) {
    posix_kernel *k = posix_kernel_create(0);
    int fds[2];
    posix_kernel_pipe(k, fds);

    /* Dup write end */
    int dup_write = posix_kernel_dup(k, fds[1]);
    /* Guest duplicate creation is checked in descriptor-flags.wast. */

    /* Close original write end — dup still holds it, so no hangup */
    posix_kernel_close(k, fds[1]);
    int r = posix_kernel_query_readiness(k, fds[0]);
    CHECK((r & POSIX_POLL_HUP) == 0, "no hangup with dup writer still open");

    /* Close dup write end — now read end gets hangup */
    posix_kernel_close(k, dup_write);
    r = posix_kernel_query_readiness(k, fds[0]);
    CHECK((r & POSIX_POLL_HUP) != 0, "hangup after all writers closed");

    posix_kernel_close(k, fds[0]);
    posix_kernel_destroy(k);
}

/* --- Close --- */

static void test_close(void) {
    posix_kernel *k = posix_kernel_create(1);

    /* Guest close results moved to guest-session-terminal-descriptors.wast.
     * Keep the close setup and all three raw readiness/lifetime checks. */
    posix_kernel_close(k, 0);
    CHECK(posix_kernel_query_readiness(k, 0) == -POSIX_EBADF,
          "fd 0 closed");
    /* fds 1 and 2 still point to the terminal OFD */
    CHECK(posix_kernel_query_readiness(k, 1) >= 0, "fd 1 still open");
    CHECK(posix_kernel_query_readiness(k, 2) >= 0, "fd 2 still open");

    posix_kernel_destroy(k);
}

/* --- Isolation: two kernels cannot see each other's state --- */

static void test_isolation(void) {
    posix_kernel *k1 = posix_kernel_create(1);
    posix_kernel *k2 = posix_kernel_create(1);

    /* Enqueue input in k1 → k2 unaffected */
    const uint8_t input[] = "data";
    posix_kernel_terminal_enqueue(k1, 0, input, 4);

    int r1 = posix_kernel_query_readiness(k1, 0);
    int r2 = posix_kernel_query_readiness(k2, 0);
    CHECK((r1 & POSIX_POLL_IN) != 0, "k1 fd 0 readable");
    CHECK((r2 & POSIX_POLL_IN) == 0, "k2 fd 0 not readable");

    /* Create pipe in k1 → k2 has no pipe */
    int fds[2];
    posix_kernel_pipe(k1, fds);
    CHECK(posix_kernel_query_readiness(k1, fds[0]) >= 0,
          "k1 pipe read end open");
    CHECK(posix_kernel_query_readiness(k2, fds[0]) == -POSIX_EBADF,
          "k2 same fd number is closed");

    /* Close fd 0 in k1 → k2 unaffected */
    posix_kernel_close(k1, 0);
    CHECK(posix_kernel_query_readiness(k1, 0) == -POSIX_EBADF,
          "k1 fd 0 closed");
    CHECK(posix_kernel_query_readiness(k2, 0) >= 0,
          "k2 fd 0 still open");

    posix_kernel_destroy(k1);
    posix_kernel_destroy(k2);
}

/* --- Edge cases --- */

static void test_edge_cases(void) {
    posix_kernel *k = posix_kernel_create(0);

    /* Closed/wrong-end descriptors and zero counts now run through guest
     * imports. C NULL pointers are distinct from guest linear-memory offsets. */
    /* Read/write with NULL buf */
    CHECK(posix_kernel_read(k, 0, NULL, 8) == -POSIX_EINVAL, "read NULL buf");
    CHECK(posix_kernel_write(k, 0, NULL, 8) == -POSIX_EINVAL, "write NULL buf");

    int fds[2];
    posix_kernel_pipe(k, fds);

    /* Allocation order is checked in descriptor-flags.wast. */

    /* NULL args */
    CHECK(posix_kernel_pipe(k, NULL) == -POSIX_EINVAL, "pipe NULL fds");
    CHECK(posix_kernel_terminal_enqueue(k, 0, NULL, 5) == -POSIX_EINVAL,
          "enqueue NULL data");

    posix_kernel_destroy(k);
}

/* Descriptor exhaustion now runs through descriptor-flags.wast. */

static void test_close_on_exec(void) {
    posix_kernel *k = posix_kernel_create(0);
    /* Guest flag/dup checks moved to descriptor-flags.wast. Keep real setup
     * for the direct close-on-exec and clone ownership boundaries below. */
    int fds[2] = {-1, -1};
    posix_kernel_pipe(k, fds);
    posix_kernel_set_cloexec(k, fds[1], POSIX_FD_CLOEXEC);
    int duplicate = posix_kernel_dup(k, fds[1]);
    posix_kernel_set_cloexec(k, duplicate, POSIX_FD_CLOEXEC);
    posix_kernel_dup2(k, fds[0], duplicate);
    posix_kernel_close_on_exec(k);
    CHECK(posix_kernel_get_cloexec(k, fds[1]) == -POSIX_EBADF,
          "exec closes marked descriptor");
    CHECK(posix_kernel_get_cloexec(k, duplicate) == 0,
          "exec preserves unmarked duplicate");
    posix_kernel *child = posix_kernel_clone(k);
    CHECK(child && posix_kernel_get_cloexec(child, duplicate) == 0,
          "unmarked descriptor survives clone");
    if (child) posix_kernel_destroy(child);
    posix_kernel_destroy(k);
}

static void test_shared_memory_names(void) {
    posix_kernel *kernel = posix_kernel_create(0);
    posix_kernel *child;
    posix_kernel *independent = posix_kernel_create(0);
    uint8_t value = 0;
    int fd;
    CHECK(kernel != NULL && independent != NULL, "create shared-memory kernels");
    CHECK(posix_kernel_set_shm_namespace(independent,
              kernel->shm_namespace) == 0,
          "attach independent kernel to shared-memory namespace");
    CHECK(posix_kernel_set_credentials(kernel, 1000, 1000) == 0 &&
          posix_kernel_shm_open(kernel, (const uint8_t *)"/denied-create", 14,
                                POSIX_O_CREAT | POSIX_O_RDWR, 0001) ==
              -POSIX_EACCES,
          "failed named-object creation is denied atomically");
    CHECK(posix_kernel_set_credentials(kernel, 0, 0) == 0 &&
          posix_kernel_shm_open(kernel, (const uint8_t *)"/denied-create", 14,
                                0, 0) == -POSIX_ENOENT,
          "failed named-object creation does not publish a name");
    fd = posix_kernel_shm_open(kernel, (const uint8_t *)"/object", 7,
                               POSIX_O_CREAT | POSIX_O_RDWR, 0600);
    /* Guest create/size/exclusive checks moved to shared-memory.wast.
     * Keep real setup for offset-independent reads, credentials and clones. */
    posix_kernel_ftruncate(kernel, fd, 4);
    CHECK(posix_kernel_file_write_at(kernel, fd, 0, "S", 1) == 1,
          "write named shared-memory object");
    int independent_fd = posix_kernel_shm_open(
        independent, (const uint8_t *)"/object", 7, 0, 0);
    CHECK(posix_kernel_set_credentials(independent, 1000, 1000) == 0 &&
          posix_kernel_shm_open(independent, (const uint8_t *)"/object", 7,
                                0, 0) == -POSIX_EACCES,
          "shared-memory mode denies an unrelated user");
    CHECK(posix_kernel_set_credentials(independent, 0, 0) == 0,
          "restore root credentials for shared-memory read");
    CHECK(independent_fd >= 0 &&
          posix_kernel_file_read_at(independent, independent_fd, 0,
                                     &value, 1) == 1 && value == 'S',
          "independent kernel opens shared-memory object");
    child = posix_kernel_clone(kernel);
    CHECK(child != NULL, "clone shared-memory kernel");
    /* Guest unlink/name lookup checks moved; keep the unlink setup before
     * direct object reads through the original and cloned descriptors. */
    posix_kernel_shm_unlink(kernel, (const uint8_t *)"/object", 7);
    CHECK(posix_kernel_file_read_at(kernel, fd, 0, &value, 1) == 1 &&
          value == 'S', "open descriptor survives shared-memory unlink");
    CHECK(child && posix_kernel_file_read_at(child, 0, 0, &value, 1) == 1 &&
          value == 'S', "forked descriptor retains shared-memory object");
    posix_kernel_close(kernel, fd);
    if (independent_fd >= 0) posix_kernel_close(independent, independent_fd);
    if (child) posix_kernel_destroy(child);
    if (independent) posix_kernel_destroy(independent);
    posix_kernel_destroy(kernel);
}

static void test_foreground_process_group_routing(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_termios termios;
    posix_fd_set readfds;
    posix_sigset empty = {{0, 0, 0, 0}};
    posix_timespec zero = {0, 0};
    const uint8_t intr = 3;
    /* Guest identity/get/set/termios checks moved to guest-session-process-groups.wast.
     * Keep real setup for host injection and private signal-state checks. */
    posix_kernel_setpgid(k, 2);
    posix_kernel_tcgetattr(k, 0, &termios);
    termios.lflag |= POSIX_TERMIOS_LFLAG_ISIG;
    termios.cc[POSIX_TERMIOS_VINTR] = 3;
    posix_kernel_tcsetattr(k, 0, &termios);
    CHECK(posix_kernel_terminal_enqueue(k, 0, &intr, 1) == 0,
          "enqueue background interrupt");
    CHECK(!posix_kernel_signal_pending(k, 2),
          "background group received terminal signal");
    posix_kernel_terminal_set_foreground_pgid(k, 0, 2);
    posix_fd_zero(&readfds);
    posix_fd_set_bit(0, &readfds);
    CHECK(posix_kernel_pselect(k, 1, &readfds, NULL, NULL, &zero, &empty) ==
              -POSIX_EINTR,
          "foreground group did not receive queued interrupt");
    CHECK(!posix_kernel_signal_pending(k, 2),
          "foreground interrupt was not consumed");
    posix_kernel_destroy(k);
}

int main(void) {
    test_lifecycle();
    test_invalid_fd();
    test_terminal_readiness();
    test_file_read_at();
    test_fork_path_publication();
    test_directory_metadata();
    test_creation_mask();
    test_terminal_modes();
    test_terminal_output_lengths();
    test_terminal_vtime();
    test_pipe_readiness();
    test_pipe_close_transitions();
    test_pipe_full();
    test_dup();
    test_pipe_dup_readiness();
    test_close();
    test_isolation();
    test_edge_cases();
    test_foreground_process_group_routing();
    test_close_on_exec();
    test_shared_memory_names();

    if (failures)
        fprintf(stderr, "%d/%d tests FAILED\n", failures, tests);
    else
        printf("posix-kernel: %d tests passed\n", tests);
    return failures ? 1 : 0;
}
