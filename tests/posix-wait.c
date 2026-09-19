/* Native deterministic tests for engine-owned descriptor waits. */
#include "lib/include/kernel.h"

#include <stdio.h>
#include <stdint.h>

static int failures;
static int tests;

#define CHECK(condition, ...) do { \
    tests++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        fputc('\n', stderr); \
        failures++; \
    } \
} while (0)

typedef struct { uint64_t now; } fake_clock;

static uint64_t fake_now(void *data) {
    return ((fake_clock *)data)->now;
}

static void test_ready_before_wait(void) {
    posix_kernel *kernel = posix_kernel_create(1);
    fake_clock clock = {0};
    posix_kernel_set_clock(kernel, fake_now, &clock);
    posix_fd_set writefds;
    posix_fd_zero(&writefds);
    posix_fd_set_bit(1, &writefds);
    posix_timeval timeout = {5, 0};

    int result = posix_kernel_select(kernel, 2, NULL, &writefds, NULL,
                                     &timeout);
    CHECK(result == 1, "ready descriptor returned %d", result);
    CHECK(!posix_kernel_wait_active(kernel), "ready call left wait active");
    posix_kernel_destroy(kernel);
}

static void test_ready_after_yield(void) {
    posix_kernel *kernel = posix_kernel_create(1);
    fake_clock clock = {0};
    posix_kernel_set_clock(kernel, fake_now, &clock);
    posix_fd_set readfds;
    posix_fd_zero(&readfds);
    posix_fd_set_bit(0, &readfds);
    posix_timeval timeout = {5, 0};

    int result = posix_kernel_select(kernel, 1, &readfds, NULL, NULL,
                                     &timeout);
    CHECK(result == -POSIX_EAGAIN, "initial poll returned %d", result);
    CHECK(posix_kernel_wait_active(kernel), "initial poll did not register");
    uint64_t generation = posix_kernel_wait_generation(kernel);
    CHECK(posix_kernel_wait_poll(kernel) == POSIX_WAIT_BLOCKED,
          "empty terminal reported ready");

    uint8_t byte = 'x';
    CHECK(posix_kernel_terminal_enqueue(kernel, 0, &byte, 1) == 0,
          "terminal enqueue failed");
    CHECK(posix_kernel_wait_poll(kernel) == POSIX_WAIT_READY,
          "readiness event did not wake wait");
    result = posix_kernel_select(kernel, 1, &readfds, NULL, NULL, &timeout);
    CHECK(result == 1, "resume poll returned %d", result);
    CHECK(posix_fd_isset(0, &readfds), "resumed output omitted fd 0");
    CHECK(!posix_kernel_wait_active(kernel), "ready resume left wait active");
    CHECK(posix_kernel_wait_generation(kernel) == generation,
          "wait generation changed across readiness wakeup");
    posix_kernel_destroy(kernel);
}

static void test_timeout(void) {
    posix_kernel *kernel = posix_kernel_create(1);
    fake_clock clock = {100};
    posix_kernel_set_clock(kernel, fake_now, &clock);
    posix_fd_set readfds;
    posix_fd_zero(&readfds);
    posix_fd_set_bit(0, &readfds);
    posix_timeval timeout = {0, 500};

    int result = posix_kernel_select(kernel, 1, &readfds, NULL, NULL,
                                     &timeout);
    CHECK(result == -POSIX_EAGAIN, "timeout wait returned %d", result);
    clock.now += 500000;
    CHECK(posix_kernel_wait_poll(kernel) == POSIX_WAIT_TIMEOUT,
          "expired wait was not timed out");
    result = posix_kernel_select(kernel, 1, &readfds, NULL, NULL, &timeout);
    CHECK(result == 0, "expired select returned %d", result);
    CHECK(!posix_kernel_wait_active(kernel), "timeout left wait active");
    CHECK(!posix_fd_isset(0, &readfds), "timeout output was not cleared");
    posix_kernel_destroy(kernel);
}

static void test_cancel_and_repeat(void) {
    posix_kernel *kernel = posix_kernel_create(1);
    fake_clock clock = {0};
    posix_kernel_set_clock(kernel, fake_now, &clock);
    posix_fd_set readfds;
    posix_fd_zero(&readfds);
    posix_fd_set_bit(0, &readfds);
    posix_timeval timeout = {1, 0};

    CHECK(posix_kernel_select(kernel, 1, &readfds, NULL, NULL, &timeout) ==
              -POSIX_EAGAIN, "repeat setup did not yield");
    uint64_t generation = posix_kernel_wait_generation(kernel);
    CHECK(posix_kernel_select(kernel, 1, &readfds, NULL, NULL, &timeout) ==
              -POSIX_EAGAIN, "repeat poll did not yield");
    CHECK(posix_kernel_wait_generation(kernel) == generation,
          "repeat poll allocated a new generation");
    posix_kernel_cancel_wait(kernel);
    CHECK(!posix_kernel_wait_active(kernel), "cancel left wait active");
    CHECK(posix_kernel_wait_poll(kernel) == POSIX_WAIT_BLOCKED,
          "cancelled wait reported a result");
    posix_kernel_destroy(kernel);
}

int main(void) {
    test_ready_before_wait();
    test_ready_after_yield();
    test_timeout();
    test_cancel_and_repeat();
    if (failures) {
        fprintf(stderr, "%d/%d tests FAILED\n", failures, tests);
        return 1;
    }
    printf("posix-wait: %d tests passed\n", tests);
    return 0;
}
