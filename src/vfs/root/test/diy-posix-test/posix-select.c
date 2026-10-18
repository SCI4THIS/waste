/* posix-select.c — Native unit tests for posix_kernel_select and
 * posix_kernel_pselect: interest-set scanning, readiness detection,
 * output-set construction, timeout behavior, and error handling.
 * Guest-visible polling and terminal assertions live in select-polling.wast
 * and the shared guest-session-terminal-readiness contract. Retained C
 * checks cover NULL kernels, internal EAGAIN and cross-kernel setup.
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
    posix_fd_set rds;
    posix_fd_zero(&rds);
    /* Guest EINVAL cases moved to select-polling.wast. NULL engine-owned
     * kernels cannot be expressed through guest imports and remain in C. */
    CHECK(posix_kernel_select(NULL, 1, &rds, NULL, NULL, NULL) == -POSIX_EINVAL,
          "NULL kernel select → EINVAL");
    CHECK(posix_kernel_pselect(NULL, 1, &rds, NULL, NULL, NULL, NULL)
              == -POSIX_EINVAL,
          "NULL kernel pselect → EINVAL");
}

/* --- All-null sets --- */

static void test_null_sets(void) {
    posix_kernel *k = posix_kernel_create(1);
    /* Zero-timeout guest results moved to select-polling.wast; kernel
     * EAGAIN maps to a guest yield rather than a guest return value. */
    CHECK(posix_kernel_select(k, 0, NULL, NULL, NULL, NULL) == -POSIX_EAGAIN,
          "all-null NULL timeout → EAGAIN");
    CHECK(posix_kernel_pselect(k, 0, NULL, NULL, NULL, NULL, NULL)
              == -POSIX_EAGAIN,
          "pselect all-null NULL timeout → EAGAIN");
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

/* --- Empty sets with nonzero nfds: no interest → nothing ready --- */

static void test_empty_sets(void) {
    posix_kernel *k = posix_kernel_create(1);
    posix_fd_set rds;
    /* Bounded empty-set poll moved to WAST. Retain private EAGAIN. */
    posix_fd_zero(&rds);
    int r = posix_kernel_select(k, 3, &rds, NULL, NULL, NULL);
    CHECK(r == -POSIX_EAGAIN, "empty set NULL timeout: %d", r);
    posix_kernel_destroy(k);
}

int main(void) {
    test_einval();
    test_null_sets();
    test_would_block();
    test_isolation();
    test_empty_sets();

    if (failures) {
        fprintf(stderr, "%d/%d tests FAILED\n", failures, tests);
        return 1;
    }
    printf("posix-select: %d tests passed\n", tests);
    return 0;
}
