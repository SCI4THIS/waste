/* posix-select.c — Native unit tests for posix_kernel_select and
 * posix_kernel_pselect: interest-set scanning, readiness detection,
 * output-set construction, timeout behavior, and error handling.
 * Built with ASan/UBSan, linked against system libc. */

#include "lib/include/kernel.h"
#include "lib/include/select.h"

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

/* --- EINVAL tests --- */

static void test_einval(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_fd_set rds;
    posix_fd_zero(&rds);

    /* NULL kernel */
    CHECK(posix_kernel_select(NULL, 1, &rds, NULL, NULL, NULL) == -POSIX_EINVAL,
          "NULL kernel select → EINVAL");
    CHECK(posix_kernel_pselect(NULL, 1, &rds, NULL, NULL, NULL, NULL)
              == -POSIX_EINVAL,
          "NULL kernel pselect → EINVAL");

    /* Bad nfds */
    CHECK(posix_kernel_select(k, -1, &rds, NULL, NULL, NULL) == -POSIX_EINVAL,
          "nfds = -1 → EINVAL");
    CHECK(posix_kernel_select(k, POSIX_FD_SETSIZE + 1, &rds, NULL, NULL, NULL)
              == -POSIX_EINVAL,
          "nfds > FD_SETSIZE → EINVAL");

    /* Bad timeval */
    posix_timeval bad_tv = {-1, 0};
    CHECK(posix_kernel_select(k, 0, NULL, NULL, NULL, &bad_tv) == -POSIX_EINVAL,
          "negative tv_sec → EINVAL");
    bad_tv = (posix_timeval){0, -1};
    CHECK(posix_kernel_select(k, 0, NULL, NULL, NULL, &bad_tv) == -POSIX_EINVAL,
          "negative tv_usec → EINVAL");
    bad_tv = (posix_timeval){0, 1000000};
    CHECK(posix_kernel_select(k, 0, NULL, NULL, NULL, &bad_tv) == -POSIX_EINVAL,
          "tv_usec == 1000000 → EINVAL");

    /* Bad timespec */
    posix_timespec bad_ts = {-1, 0};
    CHECK(posix_kernel_pselect(k, 0, NULL, NULL, NULL, &bad_ts, NULL)
              == -POSIX_EINVAL,
          "negative ts_sec → EINVAL");
    bad_ts = (posix_timespec){0, -1};
    CHECK(posix_kernel_pselect(k, 0, NULL, NULL, NULL, &bad_ts, NULL)
              == -POSIX_EINVAL,
          "negative ts_nsec → EINVAL");
    bad_ts = (posix_timespec){0, 1000000000};
    CHECK(posix_kernel_pselect(k, 0, NULL, NULL, NULL, &bad_ts, NULL)
              == -POSIX_EINVAL,
          "ts_nsec == 1e9 → EINVAL");

    posix_kernel_destroy(k);
}

/* --- EBADF tests --- */

static void test_ebadf(void) {
    posix_kernel *k = posix_kernel_create(0); /* all fds closed */
    posix_timeval zero_tv = {0, 0};

    /* Closed fd in readfds */
    posix_fd_set rds;
    posix_fd_zero(&rds);
    posix_fd_set_bit(0, &rds);
    CHECK(posix_kernel_select(k, 1, &rds, NULL, NULL, &zero_tv) == -POSIX_EBADF,
          "closed fd 0 in readfds → EBADF");

    /* Closed fd in writefds */
    posix_fd_set wrs;
    posix_fd_zero(&wrs);
    posix_fd_set_bit(5, &wrs);
    CHECK(posix_kernel_select(k, 6, NULL, &wrs, NULL, &zero_tv) == -POSIX_EBADF,
          "closed fd 5 in writefds → EBADF");

    /* Closed fd in exceptfds */
    posix_fd_set exs;
    posix_fd_zero(&exs);
    posix_fd_set_bit(10, &exs);
    CHECK(posix_kernel_select(k, 11, NULL, NULL, &exs, &zero_tv)
              == -POSIX_EBADF,
          "closed fd 10 in exceptfds → EBADF");

    /* pselect */
    posix_fd_zero(&rds);
    posix_fd_set_bit(3, &rds);
    posix_timespec zero_ts = {0, 0};
    CHECK(posix_kernel_pselect(k, 4, &rds, NULL, NULL, &zero_ts, NULL)
              == -POSIX_EBADF,
          "pselect closed fd → EBADF");

    posix_kernel_destroy(k);
}

/* --- EBADF: mixed open and closed fds --- */

static void test_ebadf_mixed(void) {
    posix_kernel *k = posix_kernel_create(1); /* fds 0,1,2 open */
    posix_fd_set rds;
    posix_timeval zero_tv = {0, 0};

    /* fd 0 is open, fd 5 is closed — both in readfds */
    posix_fd_zero(&rds);
    posix_fd_set_bit(0, &rds);
    posix_fd_set_bit(5, &rds);
    CHECK(posix_kernel_select(k, 6, &rds, NULL, NULL, &zero_tv) == -POSIX_EBADF,
          "mixed open+closed → EBADF");

    posix_kernel_destroy(k);
}

/* --- EBADF: fd beyond kernel table --- */

static void test_ebadf_high_fd(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_fd_set rds;
    posix_timeval zero_tv = {0, 0};

    /* fd 65 is beyond POSIX_KERNEL_FD_MAX (64) */
    posix_fd_zero(&rds);
    posix_fd_set_bit(65, &rds);
    CHECK(posix_kernel_select(k, 66, &rds, NULL, NULL, &zero_tv)
              == -POSIX_EBADF,
          "fd 65 beyond kernel table → EBADF");

    /* fd 65 in writefds */
    posix_fd_set wrs;
    posix_fd_zero(&wrs);
    posix_fd_set_bit(65, &wrs);
    CHECK(posix_kernel_select(k, 66, NULL, &wrs, NULL, &zero_tv)
              == -POSIX_EBADF,
          "fd 65 in writefds → EBADF");

    /* fd 65 in exceptfds */
    posix_fd_set exs;
    posix_fd_zero(&exs);
    posix_fd_set_bit(65, &exs);
    CHECK(posix_kernel_select(k, 66, NULL, NULL, &exs, &zero_tv)
              == -POSIX_EBADF,
          "fd 65 in exceptfds → EBADF");

    posix_kernel_destroy(k);
}

/* --- All-null sets --- */

static void test_null_sets(void) {
    posix_kernel *k = posix_kernel_create(1);

    /* nfds=0, all NULL, NULL timeout → EAGAIN (blocking deferred) */
    CHECK(posix_kernel_select(k, 0, NULL, NULL, NULL, NULL) == -POSIX_EAGAIN,
          "all-null NULL timeout → EAGAIN");

    /* nfds=0, all NULL, zero timeout → 0 */
    posix_timeval zero_tv = {0, 0};
    CHECK(posix_kernel_select(k, 0, NULL, NULL, NULL, &zero_tv) == 0,
          "all-null zero timeout → 0");

    /* pselect equivalents */
    CHECK(posix_kernel_pselect(k, 0, NULL, NULL, NULL, NULL, NULL)
              == -POSIX_EAGAIN,
          "pselect all-null NULL timeout → EAGAIN");
    posix_timespec zero_ts = {0, 0};
    CHECK(posix_kernel_pselect(k, 0, NULL, NULL, NULL, &zero_ts, NULL) == 0,
          "pselect all-null zero timeout → 0");

    posix_kernel_destroy(k);
}

/* --- Terminal write readiness --- */

static void test_terminal_write(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_fd_set wrs;
    posix_timeval zero_tv = {0, 0};

    /* fd 1 (stdout) should be write-ready */
    posix_fd_zero(&wrs);
    posix_fd_set_bit(1, &wrs);
    int r = posix_kernel_select(k, 2, NULL, &wrs, NULL, &zero_tv);
    CHECK(r == 1, "terminal write ready: count = %d (expected 1)", r);
    CHECK(posix_fd_isset(1, &wrs), "fd 1 in output writefds");

    /* Multiple terminal fds write-ready */
    posix_fd_zero(&wrs);
    posix_fd_set_bit(0, &wrs);
    posix_fd_set_bit(1, &wrs);
    posix_fd_set_bit(2, &wrs);
    r = posix_kernel_select(k, 3, NULL, &wrs, NULL, &zero_tv);
    CHECK(r == 3, "3 terminal fds write ready: count = %d", r);

    posix_kernel_destroy(k);
}

/* --- Terminal read readiness --- */

static void test_terminal_read(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_fd_set rds;
    posix_timeval zero_tv = {0, 0};

    /* No input enqueued: fd 0 not readable */
    posix_fd_zero(&rds);
    posix_fd_set_bit(0, &rds);
    int r = posix_kernel_select(k, 1, &rds, NULL, NULL, &zero_tv);
    CHECK(r == 0, "terminal no input: count = %d (expected 0)", r);
    CHECK(!posix_fd_isset(0, &rds), "fd 0 cleared when no input");

    /* Enqueue input → fd 0 becomes readable */
    uint8_t data[] = "hello";
    posix_kernel_terminal_enqueue(k, 0, data, 5);
    posix_fd_zero(&rds);
    posix_fd_set_bit(0, &rds);
    r = posix_kernel_select(k, 1, &rds, NULL, NULL, &zero_tv);
    CHECK(r == 1, "terminal with input: count = %d (expected 1)", r);
    CHECK(posix_fd_isset(0, &rds), "fd 0 set after enqueue");

    /* Drain input */
    uint8_t buf[16];
    posix_kernel_read(k, 0, buf, sizeof(buf));

    /* After drain: not readable */
    posix_fd_zero(&rds);
    posix_fd_set_bit(0, &rds);
    r = posix_kernel_select(k, 1, &rds, NULL, NULL, &zero_tv);
    CHECK(r == 0, "terminal drained: count = %d (expected 0)", r);

    /* EOF: readable (POLL_IN|POLL_HUP) */
    posix_kernel_terminal_signal_eof(k, 0);
    posix_fd_zero(&rds);
    posix_fd_set_bit(0, &rds);
    r = posix_kernel_select(k, 1, &rds, NULL, NULL, &zero_tv);
    CHECK(r == 1, "terminal EOF: count = %d (expected 1)", r);
    CHECK(posix_fd_isset(0, &rds), "fd 0 readable at EOF");

    posix_kernel_destroy(k);
}

/* --- Pipe readiness --- */

static void test_pipe(void) {
    posix_kernel *k = posix_kernel_create(0);
    int pfd[2];
    posix_kernel_pipe(k, pfd);
    posix_fd_set rds, wrs;
    posix_timeval zero_tv = {0, 0};

    /* Write end writable, read end not readable */
    posix_fd_zero(&rds);
    posix_fd_set_bit(pfd[0], &rds);
    posix_fd_zero(&wrs);
    posix_fd_set_bit(pfd[1], &wrs);
    int r = posix_kernel_select(k, pfd[1] + 1, &rds, &wrs, NULL, &zero_tv);
    CHECK(r == 1, "pipe initial: count = %d (expected 1, write only)", r);
    CHECK(!posix_fd_isset(pfd[0], &rds), "read end not ready");
    CHECK(posix_fd_isset(pfd[1], &wrs), "write end ready");

    /* Write data → read end becomes readable */
    uint8_t msg[] = "test";
    posix_kernel_write(k, pfd[1], msg, 4);
    posix_fd_zero(&rds);
    posix_fd_set_bit(pfd[0], &rds);
    r = posix_kernel_select(k, pfd[0] + 1, &rds, NULL, NULL, &zero_tv);
    CHECK(r == 1, "pipe with data: count = %d (expected 1)", r);
    CHECK(posix_fd_isset(pfd[0], &rds), "read end readable");

    /* Drain and close writer → read end hangup (readable) */
    uint8_t drain[16];
    posix_kernel_read(k, pfd[0], drain, sizeof(drain));
    posix_kernel_close(k, pfd[1]);
    posix_fd_zero(&rds);
    posix_fd_set_bit(pfd[0], &rds);
    r = posix_kernel_select(k, pfd[0] + 1, &rds, NULL, NULL, &zero_tv);
    CHECK(r == 1, "pipe writer closed: count = %d (expected 1, hangup)", r);
    CHECK(posix_fd_isset(pfd[0], &rds), "read end readable at hangup");

    posix_kernel_close(k, pfd[0]);
    posix_kernel_destroy(k);
}

/* --- Pipe broken: close reader → write end exceptional --- */

static void test_pipe_except(void) {
    posix_kernel *k = posix_kernel_create(0);
    int pfd[2];
    posix_kernel_pipe(k, pfd);
    posix_fd_set exs;
    posix_timeval zero_tv = {0, 0};

    posix_kernel_close(k, pfd[0]);
    posix_fd_zero(&exs);
    posix_fd_set_bit(pfd[1], &exs);
    int r = posix_kernel_select(k, pfd[1] + 1, NULL, NULL, &exs, &zero_tv);
    CHECK(r == 1, "broken pipe except: count = %d (expected 1)", r);
    CHECK(posix_fd_isset(pfd[1], &exs), "write end exceptional");

    posix_kernel_close(k, pfd[1]);
    posix_kernel_destroy(k);
}

/* --- Pipe full: write end not writable --- */

static void test_pipe_full(void) {
    posix_kernel *k = posix_kernel_create(0);
    int pfd[2];
    posix_kernel_pipe(k, pfd);
    posix_fd_set wrs;
    posix_timeval zero_tv = {0, 0};

    /* Fill pipe to capacity */
    uint8_t fill[POSIX_PIPE_CAPACITY];
    memset(fill, 'x', sizeof(fill));
    posix_kernel_write(k, pfd[1], fill, POSIX_PIPE_CAPACITY);

    /* Write end should not be writable */
    posix_fd_zero(&wrs);
    posix_fd_set_bit(pfd[1], &wrs);
    int r = posix_kernel_select(k, pfd[1] + 1, NULL, &wrs, NULL, &zero_tv);
    CHECK(r == 0, "pipe full write: count = %d (expected 0)", r);
    CHECK(!posix_fd_isset(pfd[1], &wrs), "write end not writable when full");

    /* Read some → writable again */
    uint8_t buf[16];
    posix_kernel_read(k, pfd[0], buf, sizeof(buf));
    posix_fd_zero(&wrs);
    posix_fd_set_bit(pfd[1], &wrs);
    r = posix_kernel_select(k, pfd[1] + 1, NULL, &wrs, NULL, &zero_tv);
    CHECK(r == 1, "pipe partial drain: count = %d (expected 1)", r);

    posix_kernel_close(k, pfd[0]);
    posix_kernel_close(k, pfd[1]);
    posix_kernel_destroy(k);
}

/* --- Multiple sets: same fd in read + write --- */

static void test_multiple_sets(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_fd_set rds, wrs;
    posix_timeval zero_tv = {0, 0};

    /* fd 0 in both read and write sets.  Terminal: write-ready only. */
    posix_fd_zero(&rds);
    posix_fd_set_bit(0, &rds);
    posix_fd_zero(&wrs);
    posix_fd_set_bit(0, &wrs);
    int r = posix_kernel_select(k, 1, &rds, &wrs, NULL, &zero_tv);
    CHECK(r == 1, "rd+wr fd 0 no input: count = %d (expected 1)", r);
    CHECK(!posix_fd_isset(0, &rds), "fd 0 not in output readfds");
    CHECK(posix_fd_isset(0, &wrs), "fd 0 in output writefds");

    /* Enqueue input → fd 0 readable and writable: count = 2 */
    uint8_t data[] = "x";
    posix_kernel_terminal_enqueue(k, 0, data, 1);
    posix_fd_zero(&rds);
    posix_fd_set_bit(0, &rds);
    posix_fd_zero(&wrs);
    posix_fd_set_bit(0, &wrs);
    r = posix_kernel_select(k, 1, &rds, &wrs, NULL, &zero_tv);
    CHECK(r == 2, "rd+wr fd 0 with input: count = %d (expected 2)", r);
    CHECK(posix_fd_isset(0, &rds), "fd 0 readable");
    CHECK(posix_fd_isset(0, &wrs), "fd 0 writable");

    posix_kernel_destroy(k);
}

/* --- Duplicated interest across all three sets --- */

static void test_dup_interest(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_fd_set rds, wrs, exs;
    posix_timeval zero_tv = {0, 0};

    /* fd 1 in all three sets.  Terminal: write-ready only. */
    posix_fd_zero(&rds);
    posix_fd_set_bit(1, &rds);
    posix_fd_zero(&wrs);
    posix_fd_set_bit(1, &wrs);
    posix_fd_zero(&exs);
    posix_fd_set_bit(1, &exs);
    int r = posix_kernel_select(k, 2, &rds, &wrs, &exs, &zero_tv);
    CHECK(r == 1, "all-3-sets terminal: count = %d (expected 1)", r);
    CHECK(!posix_fd_isset(1, &rds), "not read-ready");
    CHECK(posix_fd_isset(1, &wrs), "write-ready");
    CHECK(!posix_fd_isset(1, &exs), "not exceptional");

    posix_kernel_destroy(k);
}

/* --- Zero timeout polling --- */

static void test_zero_timeout(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_fd_set rds;
    posix_timeval zero_tv = {0, 0};

    /* No input on stdin → zero timeout returns 0 */
    posix_fd_zero(&rds);
    posix_fd_set_bit(0, &rds);
    int r = posix_kernel_select(k, 1, &rds, NULL, NULL, &zero_tv);
    CHECK(r == 0, "zero timeout no input: count = %d (expected 0)", r);
    CHECK(!posix_fd_isset(0, &rds), "output readfds cleared");

    /* pselect zero timeout */
    posix_timespec zero_ts = {0, 0};
    posix_fd_zero(&rds);
    posix_fd_set_bit(0, &rds);
    r = posix_kernel_pselect(k, 1, &rds, NULL, NULL, &zero_ts, NULL);
    CHECK(r == 0, "pselect zero timeout: count = %d (expected 0)", r);
    CHECK(!posix_fd_isset(0, &rds), "pselect output cleared");

    posix_kernel_destroy(k);
}

/* --- Non-zero/NULL timeout with nothing ready → EAGAIN --- */

static void test_would_block(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_fd_set rds;

    /* NULL timeout (indefinite): nothing readable → EAGAIN */
    posix_fd_zero(&rds);
    posix_fd_set_bit(0, &rds);
    int r = posix_kernel_select(k, 1, &rds, NULL, NULL, NULL);
    CHECK(r == -POSIX_EAGAIN,
          "NULL timeout would block: %d (expected %d)", r, -POSIX_EAGAIN);

    /* Non-zero timeout */
    posix_timeval tv = {1, 0};
    posix_fd_zero(&rds);
    posix_fd_set_bit(0, &rds);
    r = posix_kernel_select(k, 1, &rds, NULL, NULL, &tv);
    CHECK(r == -POSIX_EAGAIN, "1s timeout would block: %d", r);

    /* pselect non-zero timeout */
    posix_timespec ts = {0, 1000};
    posix_fd_zero(&rds);
    posix_fd_set_bit(0, &rds);
    r = posix_kernel_pselect(k, 1, &rds, NULL, NULL, &ts, NULL);
    CHECK(r == -POSIX_EAGAIN, "pselect non-zero timeout would block: %d", r);

    posix_kernel_destroy(k);
}

/* --- Output set construction: non-ready bits cleared --- */

static void test_output_sets(void) {
    posix_kernel *k = posix_kernel_create(0);
    int pfd[2];
    posix_kernel_pipe(k, pfd);
    posix_fd_set rds, wrs;
    posix_timeval zero_tv = {0, 0};

    /* Write data so read end is readable. */
    uint8_t msg[] = "hi";
    posix_kernel_write(k, pfd[1], msg, 2);

    /* Set both fds in readfds and writefds. */
    posix_fd_zero(&rds);
    posix_fd_set_bit(pfd[0], &rds);  /* read end: has data (readable) */
    posix_fd_set_bit(pfd[1], &rds);  /* write end: no POLL_IN */
    posix_fd_zero(&wrs);
    posix_fd_set_bit(pfd[0], &wrs);  /* read end: not writable */
    posix_fd_set_bit(pfd[1], &wrs);  /* write end: writable */

    int r = posix_kernel_select(k, pfd[1] + 1, &rds, &wrs, NULL, &zero_tv);
    CHECK(r == 2, "pipe output: count = %d (expected 2)", r);
    CHECK(posix_fd_isset(pfd[0], &rds), "read end in output readfds");
    CHECK(!posix_fd_isset(pfd[1], &rds), "write end not in output readfds");
    CHECK(!posix_fd_isset(pfd[0], &wrs), "read end not in output writefds");
    CHECK(posix_fd_isset(pfd[1], &wrs), "write end in output writefds");

    posix_kernel_close(k, pfd[0]);
    posix_kernel_close(k, pfd[1]);
    posix_kernel_destroy(k);
}

/* --- Ready count accuracy: counts bits, not unique fds --- */

static void test_count(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_fd_set rds, wrs, exs;
    posix_timeval zero_tv = {0, 0};

    /* Enqueue input: fd 0 readable and writable.
       Terminal: fds 0,1,2 always writable. */
    uint8_t data[] = "a";
    posix_kernel_terminal_enqueue(k, 0, data, 1);

    posix_fd_zero(&rds);
    posix_fd_set_bit(0, &rds);
    posix_fd_zero(&wrs);
    posix_fd_set_bit(0, &wrs);
    posix_fd_set_bit(1, &wrs);
    posix_fd_set_bit(2, &wrs);
    posix_fd_zero(&exs);
    posix_fd_set_bit(0, &exs);

    int r = posix_kernel_select(k, 3, &rds, &wrs, &exs, &zero_tv);
    /* fd 0: readable (1) + writable (1) = 2
       fd 1: writable (1)
       fd 2: writable (1)
       Total = 4 */
    CHECK(r == 4, "count accuracy: %d (expected 4)", r);
    CHECK(posix_fd_isset(0, &rds), "fd 0 readable");
    CHECK(posix_fd_isset(0, &wrs), "fd 0 writable");
    CHECK(!posix_fd_isset(0, &exs), "fd 0 no except");
    CHECK(posix_fd_isset(1, &wrs), "fd 1 writable");
    CHECK(posix_fd_isset(2, &wrs), "fd 2 writable");

    posix_kernel_destroy(k);
}

/* --- nfds limits scan range --- */

static void test_nfds_limit(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_fd_set wrs;
    posix_timeval zero_tv = {0, 0};

    /* Set fd 0 and fd 2 in writefds but nfds=2 → fd 2 is not scanned.
       Output replaces the set: only fd 0 should remain. */
    posix_fd_zero(&wrs);
    posix_fd_set_bit(0, &wrs);
    posix_fd_set_bit(2, &wrs);
    int r = posix_kernel_select(k, 2, NULL, &wrs, NULL, &zero_tv);
    CHECK(r == 1, "nfds=2 skips fd 2: count = %d (expected 1)", r);
    CHECK(posix_fd_isset(0, &wrs), "fd 0 in output");
    CHECK(!posix_fd_isset(2, &wrs), "fd 2 not in output (nfds limited)");

    posix_kernel_destroy(k);
}

/* --- High nfds with low fds: no false EBADF --- */

static void test_high_nfds(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_fd_set rds;
    posix_timeval zero_tv = {0, 0};

    /* nfds=100, only fd 0 set.  No bits beyond kernel table → no EBADF. */
    posix_fd_zero(&rds);
    posix_fd_set_bit(0, &rds);
    int r = posix_kernel_select(k, 100, &rds, NULL, NULL, &zero_tv);
    CHECK(r == 0, "high nfds low fd: count = %d (expected 0)", r);

    posix_kernel_destroy(k);
}

/* --- Cross-kernel isolation --- */

static void test_isolation(void) {
    posix_kernel *k1 = posix_kernel_create(1);
    posix_kernel *k2 = posix_kernel_create(0);
    posix_fd_set wrs;
    posix_timeval zero_tv = {0, 0};

    /* k1 has terminals, k2 has no open fds */
    posix_fd_zero(&wrs);
    posix_fd_set_bit(1, &wrs);
    int r1 = posix_kernel_select(k1, 2, NULL, &wrs, NULL, &zero_tv);
    CHECK(r1 == 1, "k1 terminal writable: %d", r1);

    posix_fd_zero(&wrs);
    posix_fd_set_bit(1, &wrs);
    int r2 = posix_kernel_select(k2, 2, NULL, &wrs, NULL, &zero_tv);
    CHECK(r2 == -POSIX_EBADF, "k2 fd 1 closed: %d", r2);

    posix_kernel_destroy(k1);
    posix_kernel_destroy(k2);
}

/* --- pselect basic and sigmask --- */

static void test_pselect(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_fd_set wrs;
    posix_timespec zero_ts = {0, 0};

    /* Basic write readiness */
    posix_fd_zero(&wrs);
    posix_fd_set_bit(1, &wrs);
    int r = posix_kernel_pselect(k, 2, NULL, &wrs, NULL, &zero_ts, NULL);
    CHECK(r == 1, "pselect write ready: count = %d", r);
    CHECK(posix_fd_isset(1, &wrs), "fd 1 in output");

    /* sigmask is ignored (Stage 6) but should not crash */
    posix_sigset fake_mask = {{0, 0, 0, 0}};
    posix_fd_zero(&wrs);
    posix_fd_set_bit(2, &wrs);
    r = posix_kernel_pselect(k, 3, NULL, &wrs, NULL, &zero_ts, &fake_mask);
    CHECK(r == 1, "pselect with sigmask: count = %d", r);

    posix_kernel_destroy(k);
}

/* --- Empty sets with nonzero nfds: no interest → nothing ready --- */

static void test_empty_sets(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_fd_set rds;
    posix_timeval zero_tv = {0, 0};

    /* All-zero set with nfds=3: no bits set, so nothing to check */
    posix_fd_zero(&rds);
    int r = posix_kernel_select(k, 3, &rds, NULL, NULL, &zero_tv);
    CHECK(r == 0, "empty set zero timeout: count = %d (expected 0)", r);

    /* NULL timeout with empty set → EAGAIN */
    posix_fd_zero(&rds);
    r = posix_kernel_select(k, 3, &rds, NULL, NULL, NULL);
    CHECK(r == -POSIX_EAGAIN, "empty set NULL timeout: %d", r);

    posix_kernel_destroy(k);
}

/* --- Dup'd descriptors in sets --- */

static void test_dup_fd(void) {
    posix_kernel *k = posix_kernel_create(0);
    int pfd[2];
    posix_kernel_pipe(k, pfd);
    int dup_rd = posix_kernel_dup(k, pfd[0]);
    posix_fd_set rds;
    posix_timeval zero_tv = {0, 0};

    /* Write data → both read end and dup'd read end are readable */
    uint8_t msg[] = "xyz";
    posix_kernel_write(k, pfd[1], msg, 3);
    posix_fd_zero(&rds);
    posix_fd_set_bit(pfd[0], &rds);
    posix_fd_set_bit(dup_rd, &rds);
    int nfds = (pfd[0] > dup_rd ? pfd[0] : dup_rd) + 1;
    int r = posix_kernel_select(k, nfds, &rds, NULL, NULL, &zero_tv);
    CHECK(r == 2, "dup'd read fds: count = %d (expected 2)", r);
    CHECK(posix_fd_isset(pfd[0], &rds), "original read end set");
    CHECK(posix_fd_isset(dup_rd, &rds), "dup'd read end set");

    posix_kernel_close(k, dup_rd);
    posix_kernel_close(k, pfd[0]);
    posix_kernel_close(k, pfd[1]);
    posix_kernel_destroy(k);
}

int main(void) {
    test_einval();
    test_ebadf();
    test_ebadf_mixed();
    test_ebadf_high_fd();
    test_null_sets();
    test_terminal_write();
    test_terminal_read();
    test_pipe();
    test_pipe_except();
    test_pipe_full();
    test_multiple_sets();
    test_dup_interest();
    test_zero_timeout();
    test_would_block();
    test_output_sets();
    test_count();
    test_nfds_limit();
    test_high_nfds();
    test_isolation();
    test_pselect();
    test_empty_sets();
    test_dup_fd();

    if (failures) {
        fprintf(stderr, "%d/%d tests FAILED\n", failures, tests);
        return 1;
    }
    printf("posix-select: %d tests passed\n", tests);
    return 0;
}
