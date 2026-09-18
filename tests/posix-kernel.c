/* posix-kernel.c — Native unit tests for the per-sandbox POSIX kernel:
 * lifecycle, descriptor table, terminal and pipe readiness, dup/close,
 * read/write, and cross-kernel isolation.
 * Built with ASan/UBSan, linked against system libc. */

#include "lib/include/kernel.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;
static int tests = 0;

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
    int n = posix_kernel_read(k, 0, buf, sizeof(buf));
    CHECK(n == 5, "read returns 5 bytes");
    CHECK(memcmp(buf, "hello", 5) == 0, "read data matches");
    r = posix_kernel_query_readiness(k, 0);
    CHECK((r & POSIX_POLL_IN) == 0, "terminal not readable after drain");

    /* EOF → readable + hangup */
    CHECK(posix_kernel_terminal_signal_eof(k, 0) == 0, "signal eof ok");
    r = posix_kernel_query_readiness(k, 0);
    CHECK((r & POSIX_POLL_IN) != 0, "terminal readable at eof");
    CHECK((r & POSIX_POLL_HUP) != 0, "terminal hangup at eof");

    /* Read at EOF returns 0 */
    n = posix_kernel_read(k, 0, buf, sizeof(buf));
    CHECK(n == 0, "read at eof returns 0");

    /* Enqueue on non-terminal fails */
    CHECK(posix_kernel_terminal_enqueue(k, 5, input, 5) == -POSIX_EBADF,
          "enqueue on closed fd");

    /* Terminal write always succeeds */
    n = posix_kernel_write(k, 1, input, 5);
    CHECK(n == 5, "terminal write returns count");

    posix_kernel_destroy(k);
}

/* --- Pipe creation and readiness --- */

static void test_pipe_readiness(void) {
    posix_kernel *k = posix_kernel_create(0);
    int fds[2];

    CHECK(posix_kernel_pipe(k, fds) == 0, "pipe creation");
    CHECK(fds[0] >= 0 && fds[0] < POSIX_KERNEL_FD_MAX, "read fd valid");
    CHECK(fds[1] >= 0 && fds[1] < POSIX_KERNEL_FD_MAX, "write fd valid");
    CHECK(fds[0] != fds[1], "read != write fd");

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
    int n = posix_kernel_write(k, fds[1], data, 4);
    CHECK(n == 4, "pipe write 4 bytes");

    r = posix_kernel_query_readiness(k, fds[0]);
    CHECK((r & POSIX_POLL_IN) != 0, "pipe read end readable after write");

    /* Read data back */
    uint8_t buf[32];
    n = posix_kernel_read(k, fds[0], buf, sizeof(buf));
    CHECK(n == 4, "pipe read 4 bytes");
    CHECK(memcmp(buf, "test", 4) == 0, "pipe data matches");

    r = posix_kernel_query_readiness(k, fds[0]);
    CHECK((r & POSIX_POLL_IN) == 0, "pipe read end not readable after drain");

    /* Read on empty pipe with writers open → EAGAIN */
    n = posix_kernel_read(k, fds[0], buf, sizeof(buf));
    CHECK(n == -POSIX_EAGAIN, "pipe read empty returns EAGAIN");

    posix_kernel_destroy(k);
}

/* --- Pipe EOF and broken pipe --- */

static void test_pipe_close_transitions(void) {
    posix_kernel *k = posix_kernel_create(0);
    int fds[2];

    /* Close write end → read end gets HANGUP */
    CHECK(posix_kernel_pipe(k, fds) == 0, "pipe for close test");
    const uint8_t data[] = "abc";
    posix_kernel_write(k, fds[1], data, 3);
    posix_kernel_close(k, fds[1]);

    int r = posix_kernel_query_readiness(k, fds[0]);
    CHECK((r & POSIX_POLL_IN) != 0, "read end readable (data + eof)");
    CHECK((r & POSIX_POLL_HUP) != 0, "read end hangup after write close");

    /* Read remaining data, then EOF */
    uint8_t buf[32];
    int n = posix_kernel_read(k, fds[0], buf, sizeof(buf));
    CHECK(n == 3, "read remaining data");
    n = posix_kernel_read(k, fds[0], buf, sizeof(buf));
    CHECK(n == 0, "read EOF after writer closed");

    posix_kernel_close(k, fds[0]);

    /* Close read end → write end gets broken pipe */
    CHECK(posix_kernel_pipe(k, fds) == 0, "pipe for broken test");
    posix_kernel_close(k, fds[0]);

    r = posix_kernel_query_readiness(k, fds[1]);
    CHECK((r & POSIX_POLL_ERR) != 0, "write end error after read close");
    CHECK((r & POSIX_POLL_OUT) == 0, "write end not writable after read close");

    n = posix_kernel_write(k, fds[1], data, 3);
    CHECK(n == -POSIX_EPIPE, "write to broken pipe returns EPIPE");

    posix_kernel_close(k, fds[1]);
    posix_kernel_destroy(k);
}

/* --- Pipe full --- */

static void test_pipe_full(void) {
    posix_kernel *k = posix_kernel_create(0);
    int fds[2];
    CHECK(posix_kernel_pipe(k, fds) == 0, "pipe for full test");

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
    CHECK(newfd >= 3, "dup returns fd >= 3");
    CHECK(posix_kernel_query_readiness(k, newfd) ==
          posix_kernel_query_readiness(k, 0), "dup'd fd same readiness");

    /* Enqueue on original → duped fd also readable */
    const uint8_t input[] = "hi";
    posix_kernel_terminal_enqueue(k, 0, input, 2);
    int r = posix_kernel_query_readiness(k, newfd);
    CHECK((r & POSIX_POLL_IN) != 0, "dup'd fd readable after enqueue on original");

    /* Read from dup drains shared buffer */
    uint8_t buf[32];
    int n = posix_kernel_read(k, newfd, buf, sizeof(buf));
    CHECK(n == 2, "read from dup'd fd");
    r = posix_kernel_query_readiness(k, 0);
    CHECK((r & POSIX_POLL_IN) == 0, "original not readable after dup read");

    /* Close dup → original still works */
    posix_kernel_close(k, newfd);
    CHECK(posix_kernel_query_readiness(k, newfd) == -POSIX_EBADF,
          "dup'd fd closed");
    CHECK(posix_kernel_query_readiness(k, 0) >= 0,
          "original still open after dup close");

    /* Dup invalid fd */
    CHECK(posix_kernel_dup(k, 50) == -POSIX_EBADF, "dup closed fd");
    CHECK(posix_kernel_dup(k, -1) == -POSIX_EINVAL, "dup negative fd");

    posix_kernel_destroy(k);
}

/* --- Dup2 --- */

static void test_dup2(void) {
    posix_kernel *k = posix_kernel_create(0);
    int fds[2];
    posix_kernel_pipe(k, fds);

    /* Dup2 pipe read to fd 10 */
    int result = posix_kernel_dup2(k, fds[0], 10);
    CHECK(result == 10, "dup2 returns target fd");
    CHECK(posix_kernel_query_readiness(k, 10) >= 0, "dup2 target open");

    /* Dup2 same fd → no-op */
    result = posix_kernel_dup2(k, fds[0], fds[0]);
    CHECK(result == fds[0], "dup2 same fd returns same");

    /* Dup2 overwrites open fd */
    posix_kernel_dup2(k, fds[1], fds[0]); /* overwrites read end with write end */
    int r = posix_kernel_query_readiness(k, fds[0]);
    /* fds[0] now points to write end OFD, but since we just closed the read end,
       write end should show broken pipe (no readers) */
    /* Actually, if fd 10 still holds the read end, readers > 0 */
    CHECK(r >= 0, "overwritten fd is open");

    posix_kernel_destroy(k);
}

/* --- Pipe dup affects reader/writer counts --- */

static void test_pipe_dup_readiness(void) {
    posix_kernel *k = posix_kernel_create(0);
    int fds[2];
    posix_kernel_pipe(k, fds);

    /* Dup write end */
    int dup_write = posix_kernel_dup(k, fds[1]);
    CHECK(dup_write >= 0, "dup write end");

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

    CHECK(posix_kernel_close(k, 0) == 0, "close fd 0");
    CHECK(posix_kernel_query_readiness(k, 0) == -POSIX_EBADF,
          "fd 0 closed");
    /* fds 1 and 2 still point to the terminal OFD */
    CHECK(posix_kernel_query_readiness(k, 1) >= 0, "fd 1 still open");
    CHECK(posix_kernel_query_readiness(k, 2) >= 0, "fd 2 still open");

    /* Double close */
    CHECK(posix_kernel_close(k, 0) == -POSIX_EBADF, "double close");

    /* Close invalid */
    CHECK(posix_kernel_close(k, -1) == -POSIX_EINVAL, "close negative");
    CHECK(posix_kernel_close(k, POSIX_KERNEL_FD_MAX) == -POSIX_EINVAL,
          "close out of range");

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

    /* Read/write on closed fd */
    uint8_t buf[8];
    CHECK(posix_kernel_read(k, 0, buf, 8) == -POSIX_EBADF, "read closed fd");
    CHECK(posix_kernel_write(k, 0, buf, 8) == -POSIX_EBADF, "write closed fd");

    /* Read/write with NULL buf */
    CHECK(posix_kernel_read(k, 0, NULL, 8) == -POSIX_EINVAL, "read NULL buf");
    CHECK(posix_kernel_write(k, 0, NULL, 8) == -POSIX_EINVAL, "write NULL buf");

    /* Read/write zero count */
    int fds[2];
    posix_kernel_pipe(k, fds);
    CHECK(posix_kernel_read(k, fds[0], buf, 0) == 0, "read zero count");
    CHECK(posix_kernel_write(k, fds[1], buf, 0) == 0, "write zero count");

    /* Read from write end / write to read end */
    CHECK(posix_kernel_read(k, fds[1], buf, 8) == -POSIX_EBADF,
          "read from pipe write end");
    CHECK(posix_kernel_write(k, fds[0], buf, 8) == -POSIX_EBADF,
          "write to pipe read end");

    /* Pipe creation fills fds correctly */
    int fds2[2];
    posix_kernel_pipe(k, fds2);
    CHECK(fds2[0] > fds[1], "second pipe gets higher fds");

    /* NULL args */
    CHECK(posix_kernel_pipe(k, NULL) == -POSIX_EINVAL, "pipe NULL fds");
    CHECK(posix_kernel_terminal_enqueue(k, 0, NULL, 5) == -POSIX_EINVAL,
          "enqueue NULL data");

    posix_kernel_destroy(k);
}

/* --- Fill all fd slots --- */

static void test_fd_exhaustion(void) {
    posix_kernel *k = posix_kernel_create(0);

    /* Create pipes until we run out of fds */
    int pair_count = 0;
    int all_fds[POSIX_KERNEL_FD_MAX];
    int all_fd_count = 0;
    while (1) {
        int fds[2];
        int result = posix_kernel_pipe(k, fds);
        if (result < 0) {
            CHECK(result == -POSIX_EMFILE, "exhaustion returns EMFILE");
            break;
        }
        all_fds[all_fd_count++] = fds[0];
        all_fds[all_fd_count++] = fds[1];
        pair_count++;
    }
    CHECK(pair_count == POSIX_KERNEL_FD_MAX / 2, "exactly FD_MAX/2 pipes");

    /* Dup also fails */
    CHECK(posix_kernel_dup(k, all_fds[0]) == -POSIX_EMFILE,
          "dup fails when full");

    /* Close one → dup works */
    posix_kernel_close(k, all_fds[0]);
    int newfd = posix_kernel_dup(k, all_fds[1]);
    CHECK(newfd == all_fds[0], "dup reuses lowest freed fd");

    posix_kernel_destroy(k);
}

int main(void) {
    test_lifecycle();
    test_invalid_fd();
    test_terminal_readiness();
    test_pipe_readiness();
    test_pipe_close_transitions();
    test_pipe_full();
    test_dup();
    test_dup2();
    test_pipe_dup_readiness();
    test_close();
    test_isolation();
    test_edge_cases();
    test_fd_exhaustion();

    if (failures)
        fprintf(stderr, "%d/%d tests FAILED\n", failures, tests);
    else
        printf("posix-kernel: %d tests passed\n", tests);
    return failures ? 1 : 0;
}
