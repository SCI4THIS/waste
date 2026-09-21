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
    check(&store.processes[0].capsule != &store.processes[1].capsule,
          "fork creates distinct parent and child capsules");
    check(native_store_getpid(&store) == 1 && native_store_getppid(&store) == 0,
          "parent pid and ppid are stable");
    check(native_store_active_capsule(&store) != NULL &&
          native_store_active_capsule(&store)->state == NATIVE_PROCESS_RUNNABLE,
          "parent execution capsule is selected");
    native_exec_request request;
    memset(&request, 0, sizeof(request));
    request.active = 1;
    request.pid = 1;
    check(native_store_prepare_process_exec(&store, &request) == 0 &&
          native_store_active_capsule(&store)->pending_transition ==
              NATIVE_PROCESS_TRANSITION_EXEC,
          "exec transition is prepared on parent capsule");
    native_store_abort_process_exec(&store);
    check(native_store_active_capsule(&store)->pending_transition ==
              NATIVE_PROCESS_TRANSITION_NONE,
          "exec transition abort clears pending state");
    check(native_store_set_active_process(&store, child_pid) == 0,
          "child can be selected");
    check(native_store_getpid(&store) == child_pid && native_store_getppid(&store) == 1,
          "child observes pid and parent pid");
    check(native_store_active_capsule(&store) != NULL &&
          native_store_active_capsule(&store)->state == NATIVE_PROCESS_RUNNABLE,
          "child execution capsule is selected");
    check(posix_kernel_write(store.kernel, fds[1], "x", 1) == 1,
          "child inherited shared open-file description");
    check(native_store_exit_process(&store, 7) == 0,
          "child becomes zombie with explicit nonzero status");
    check(native_store_exit_process(&store, 9) == 0,
          "duplicate child exit is idempotent");
    check(native_store_set_active_process(&store, 1) == 0,
          "parent is restored after child exit");

    int status = 0;
    check(native_store_wait_process(&store, child_pid, 0, &status) == child_pid,
          "parent reaps requested child");
    check(status == (7 << 8), "wait status uses POSIX exit encoding");
    check(native_store_wait_process(&store, child_pid, 0, &status) == -POSIX_ECHILD,
          "child can only be reaped once");
    char byte = 0;
    check(posix_kernel_read(store.kernel, fds[0], &byte, 1) == 1 && byte == 'x',
          "parent descriptors remain usable after reaping");
    check(native_store_wait_process(&store, -1, 0, &status) == -POSIX_ECHILD,
          "wait without children reports ECHILD");
    check(native_store_wait_process(&store, 0, 0, &status) == -POSIX_ECHILD,
          "empty process-group wait reports ECHILD");

    int group_child = 0;
    check(native_store_fork_process(&store, &group_child) == 0,
          "fork for group signal status");
    check(posix_kernel_setpgid(store.processes[1].kernel, 7) == 7,
          "assign child signal group");
    check(native_store_signal_process(&store, group_child, 15) == 0,
          "group member receives signal");
    check(native_store_set_active_process(&store, 1) == 0,
          "restore parent after group signal");
    check(native_store_wait_process(&store, group_child, 0, &status) == group_child &&
          status == ((128 + 15) << 8),
          "group signal status is reapable");

    posix_kernel_destroy(store.kernel);
    printf("C-engine process lifecycle: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
