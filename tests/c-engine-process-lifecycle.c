#include "store.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

static void check(int condition, const char *message) {
    checks++;
    if (!condition) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

int main(void) {
    native_store store;
    memset(&store, 0, sizeof(store));
    store.kernel = posix_kernel_create(0);
    if (!store.kernel) return 2;
    store.processes[0].used = 1;
    store.processes[0].pid = 1;
    store.processes[0].kernel = store.kernel;
    store.process_count = 1;
    store.active_pid = 1;
    store.next_pid = 2;

    int fds[2];
    check(posix_kernel_pipe(store.kernel, fds) == 0, "parent pipe creates");
    int child_pid = 0;
    check(native_store_fork_process(&store, &child_pid) == 0 && child_pid == 2,
          "fork allocates child pid");
    check(native_store_getpid(&store) == 1 && native_store_getppid(&store) == 0,
          "parent pid and ppid are stable");
    check(native_store_set_active_process(&store, child_pid) == 0,
          "child can be selected");
    check(native_store_getpid(&store) == child_pid && native_store_getppid(&store) == 1,
          "child observes pid and parent pid");
    check(posix_kernel_write(store.kernel, fds[1], "x", 1) == 1,
          "child inherited shared open-file description");
    check(native_store_exit_process(&store, 127) == 0,
          "child becomes zombie with exit status");
    check(native_store_set_active_process(&store, 1) == 0,
          "parent is restored after child exit");

    int status = 0;
    check(native_store_wait_process(&store, child_pid, 0, &status) == child_pid,
          "parent reaps requested child");
    check(status == (127 << 8), "wait status uses POSIX exit encoding");
    check(native_store_wait_process(&store, child_pid, 0, &status) == -POSIX_ECHILD,
          "child can only be reaped once");
    char byte = 0;
    check(posix_kernel_read(store.kernel, fds[0], &byte, 1) == 1 && byte == 'x',
          "parent descriptors remain usable after reaping");
    check(native_store_wait_process(&store, -1, 0, &status) == -POSIX_ECHILD,
          "wait without children reports ECHILD");
    check(native_store_wait_process(&store, 0, 0, &status) == -POSIX_ECHILD,
          "empty process-group wait reports ECHILD");

    posix_kernel_destroy(store.kernel);
    printf("C-engine process lifecycle: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
