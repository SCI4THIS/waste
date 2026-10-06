/* Native deterministic signal/pselect tests. */
#include "lib/include/kernel.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

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

static void mask_signal(posix_sigset *mask, int signal) {
    uint32_t bit = (uint32_t)(signal - 1);
    mask->words[bit / 32] |= UINT32_C(1) << (bit % 32);
}

static int mask_has(const posix_sigset *mask, int signal) {
    uint32_t bit = (uint32_t)(signal - 1);
    return (mask->words[bit / 32] >> (bit % 32)) & 1;
}

static void test_pending_before_call(void) {
    posix_kernel *kernel = posix_kernel_create(1);
    posix_fd_set readfds;
    posix_fd_zero(&readfds);
    posix_fd_set_bit(0, &readfds);
    posix_sigset empty = {{0, 0, 0, 0}};
    posix_timespec zero = {0, 0};

    CHECK(posix_kernel_signal_raise(kernel, 2) == 0, "raise failed");
    int result = posix_kernel_pselect(kernel, 1, &readfds, NULL, NULL,
                                      &zero, &empty);
    CHECK(result == -POSIX_EINTR, "pending pselect returned %d", result);
    CHECK(!posix_kernel_signal_pending(kernel, 2), "signal was not consumed");
    posix_sigset current;
    posix_kernel_get_signal_mask(kernel, &current);
    CHECK(!mask_has(&current, 2), "temporary mask was not restored");
    posix_kernel_destroy(kernel);
}

static void test_signal_after_yield(void) {
    posix_kernel *kernel = posix_kernel_create(1);
    posix_fd_set readfds;
    posix_fd_zero(&readfds);
    posix_fd_set_bit(0, &readfds);
    posix_sigset empty = {{0, 0, 0, 0}};
    posix_timespec timeout = {5, 0};

    int result = posix_kernel_pselect(kernel, 1, &readfds, NULL, NULL,
                                      &timeout, &empty);
    CHECK(result == -POSIX_EAGAIN, "pselect did not yield: %d", result);
    CHECK(posix_kernel_wait_active(kernel), "signal wait not active");
    CHECK(posix_kernel_signal_raise(kernel, 15) == 0, "raise after yield failed");
    CHECK(posix_kernel_wait_poll(kernel) == POSIX_WAIT_SIGNAL,
          "signal did not wake pselect");
    result = posix_kernel_pselect(kernel, 1, &readfds, NULL, NULL,
                                  &timeout, &empty);
    CHECK(result == -POSIX_EINTR, "signal resume returned %d", result);
    CHECK(!posix_kernel_wait_active(kernel), "interrupted wait remains active");
    posix_sigset current;
    posix_kernel_get_signal_mask(kernel, &current);
    CHECK(current.words[0] == 0 && current.words[1] == 0 &&
          current.words[2] == 0 && current.words[3] == 0,
          "mask was not restored after interruption");
    posix_kernel_destroy(kernel);
}

/* Blocked-signal observations now run through real guest imports in
 * engine-regressions/signal-masks.wast. Wait ownership stays in C below. */

static void test_simultaneous_and_restore(void) {
    posix_kernel *kernel = posix_kernel_create(1);
    posix_sigset original = {{0, 0, 0, 0}};
    mask_signal(&original, 15);
    posix_kernel_set_signal_mask(kernel, &original);
    posix_sigset temporary = {{0, 0, 0, 0}};
    posix_fd_set readfds;
    posix_fd_zero(&readfds);
    posix_fd_set_bit(0, &readfds);
    posix_timespec zero = {0, 0};

    CHECK(posix_kernel_signal_raise(kernel, 2) == 0, "simultaneous raise failed");
    uint8_t input = 'a';
    CHECK(posix_kernel_terminal_enqueue(kernel, 0, &input, 1) == 0,
          "simultaneous input failed");
    int result = posix_kernel_pselect(kernel, 1, &readfds, NULL, NULL,
                                      &zero, &temporary);
    CHECK(result == -POSIX_EINTR, "signal/readiness result %d", result);
    posix_sigset current;
    posix_kernel_get_signal_mask(kernel, &current);
    CHECK(mask_has(&current, 15), "original mask not restored");

    posix_fd_zero(&readfds);
    posix_fd_set_bit(0, &readfds);
    posix_sigset temporary_blocked = {{0, 0, 0, 0}};
    mask_signal(&temporary_blocked, 2);
    result = posix_kernel_pselect(kernel, 1, &readfds, NULL, NULL,
                                  &zero, &temporary_blocked);
    CHECK(result == 1, "blocked signal/readiness result %d", result);
    posix_kernel_get_signal_mask(kernel, &current);
    CHECK(mask_has(&current, 15) && !mask_has(&current, 2),
          "mask restoration after ready failed");
    posix_kernel_destroy(kernel);
}

static void test_cancel_and_error_restore(void) {
    posix_kernel *kernel = posix_kernel_create(1);
    posix_sigset temporary = {{0, 0, 0, 0}};
    mask_signal(&temporary, 2);
    posix_fd_set readfds;
    posix_fd_zero(&readfds);
    posix_fd_set_bit(0, &readfds);
    posix_timespec timeout = {1, 0};
    CHECK(posix_kernel_pselect(kernel, 1, &readfds, NULL, NULL, &timeout,
                               &temporary) == -POSIX_EAGAIN,
          "cancel setup did not yield");
    posix_kernel_cancel_wait(kernel);
    posix_sigset current;
    posix_kernel_get_signal_mask(kernel, &current);
    CHECK(!mask_has(&current, 2), "cancel did not restore mask");
    posix_fd_zero(&readfds);
    posix_fd_set_bit(63, &readfds);
    CHECK(posix_kernel_pselect(kernel, 64, &readfds, NULL, NULL, &timeout,
                               &temporary) == -POSIX_EBADF,
          "invalid descriptor did not fail");
    posix_kernel_get_signal_mask(kernel, &current);
    CHECK(!mask_has(&current, 2), "error did not restore mask");
    posix_kernel_destroy(kernel);
}

static void test_signal_dispositions(void) {
    posix_kernel *kernel = posix_kernel_create(1);
    posix_signal_disposition disposition;
    uint32_t handler;
    CHECK(posix_kernel_signal_get_disposition(kernel, 2, &disposition) == 0,
          "get default disposition");
    CHECK(disposition == POSIX_SIGNAL_DEFAULT, "signals start default");
    CHECK(posix_kernel_signal_get_handler(kernel, 2, &handler) == 0 &&
          handler == POSIX_SIG_DFL, "signals start with SIG_DFL handler");
    CHECK(posix_kernel_signal_set_disposition(kernel, 2,
              POSIX_SIGNAL_IGNORE) == 0, "install ignore disposition");
    CHECK(posix_kernel_signal_raise(kernel, 2) == 0,
          "ignored signal raise succeeds");
    CHECK(!posix_kernel_signal_pending(kernel, 2),
          "ignored signal is not pending");
    CHECK(posix_kernel_signal_set_disposition(kernel, 2,
              POSIX_SIGNAL_HANDLER) == 0, "install handler disposition");
    CHECK(posix_kernel_signal_set_handler(kernel, 2, 0x1234u) == 0,
          "install handler pointer");
    CHECK(posix_kernel_signal_get_handler(kernel, 2, &handler) == 0 &&
          handler == 0x1234u, "handler pointer round trip");
    CHECK(posix_kernel_signal_raise(kernel, 2) == 0,
          "caught signal raise succeeds");
    CHECK(posix_kernel_signal_pending(kernel, 2),
          "caught signal remains pending for delivery");
    CHECK(posix_kernel_signal_set_disposition(kernel, 2,
              POSIX_SIGNAL_IGNORE) == 0, "reset signal to ignore");
    CHECK(!posix_kernel_signal_pending(kernel, 2),
          "changing to ignore clears pending signal");
    CHECK(posix_kernel_signal_set_disposition(kernel, POSIX_SIGKILL,
              POSIX_SIGNAL_IGNORE) == -POSIX_EINVAL,
          "SIGKILL cannot be ignored");
    CHECK(posix_kernel_signal_set_disposition(kernel, POSIX_SIGSTOP,
              POSIX_SIGNAL_HANDLER) == -POSIX_EINVAL,
          "SIGSTOP cannot be caught");
    CHECK(posix_kernel_signal_set_disposition(kernel, 2, 99) == -POSIX_EINVAL,
          "invalid disposition rejected");
    posix_kernel_destroy(kernel);
}

static void test_handler_mask_round_trip(void) {
    posix_kernel *kernel = posix_kernel_create(1);
    posix_sigset action = {{0, 0, 0, 0}};
    posix_sigset saved = {{0, 0, 0, 0}};
    posix_sigset current;
    mask_signal(&action, 15);
    CHECK(posix_kernel_signal_set_action_mask(kernel, 2, &action) == 0,
          "set handler mask failed");
    posix_sigset decoded;
    CHECK(posix_kernel_signal_get_action_mask(kernel, 2, &decoded) == 0 &&
          mask_has(&decoded, 15), "handler mask round trip failed");
    CHECK(posix_kernel_signal_enter_handler(kernel, 2, &saved) == 0,
          "enter handler failed");
    posix_kernel_get_signal_mask(kernel, &current);
    CHECK(mask_has(&current, 2) && mask_has(&current, 15),
          "handler did not apply self/action mask");
    posix_kernel_signal_leave_handler(kernel, &saved);
    posix_kernel_get_signal_mask(kernel, &current);
    CHECK(current.words[0] == 0 && current.words[1] == 0 &&
          current.words[2] == 0 && current.words[3] == 0,
          "handler mask was not restored");
    posix_kernel_destroy(kernel);
}

int main(void) {
    test_pending_before_call();
    test_signal_after_yield();
    test_simultaneous_and_restore();
    test_cancel_and_error_restore();
    test_signal_dispositions();
    test_handler_mask_round_trip();
    if (failures) {
        fprintf(stderr, "%d/%d tests FAILED\n", failures, tests);
        return 1;
    }
    printf("posix-signal: %d tests passed\n", tests);
    return 0;
}
