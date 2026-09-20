#include "store.h"

#include <string.h>

static native_process *find_process(native_store *store, int pid) {
    if (!store || pid <= 0) return NULL;
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++)
        if (store->processes[i].used && store->processes[i].pid == pid)
            return &store->processes[i];
    return NULL;
}

static const native_process *find_process_const(const native_store *store, int pid) {
    return find_process((native_store *)store, pid);
}

static native_process *active_process(native_store *store) {
    return find_process(store, store ? store->active_pid : 0);
}

int native_store_getpid(const native_store *store) {
    const native_process *process = store ? find_process_const(store, store->active_pid) : NULL;
    return process ? process->pid : -POSIX_EINVAL;
}

int native_store_getppid(const native_store *store) {
    const native_process *process = store ? find_process_const(store, store->active_pid) : NULL;
    return process ? process->ppid : -POSIX_EINVAL;
}

int native_store_set_active_process(native_store *store, int pid) {
    native_process *process = find_process(store, pid);
    if (!process || process->zombie) return -POSIX_ECHILD;
    store->active_pid = pid;
    store->kernel = process->kernel;
    store->kernel_terminal = process->kernel && process->kernel->fds[0].ofd != NULL;
    return 0;
}

int native_store_fork_process(native_store *store, int *pid_out) {
    native_process *parent = active_process(store);
    if (!parent || !pid_out) return -POSIX_EFAULT;
    int slot = -1;
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++)
        if (!store->processes[i].used) { slot = i; break; }
    if (slot < 0) return -POSIX_ENOMEM;
    posix_kernel *kernel = posix_kernel_clone(parent->kernel);
    if (!kernel) return -POSIX_ENOMEM;
    if (store->next_pid <= 0) store->next_pid = 2;
    int pid = store->next_pid;
    if (store->next_pid == INT32_MAX) store->next_pid = -1;
    else store->next_pid++;
    native_process *child = &store->processes[slot];
    memset(child, 0, sizeof(*child));
    child->used = 1;
    child->pid = pid;
    child->ppid = parent->pid;
    child->kernel = kernel;
    store->process_count++;
    *pid_out = pid;
    return 0;
}

int native_store_exit_process(native_store *store, int status) {
    native_process *process = active_process(store);
    if (!process) return -POSIX_EINVAL;
    if (process->pid == 1) return -POSIX_EINVAL;
    process->exit_status = (status & 0xff) << 8;
    process->zombie = 1;
    return 0;
}

int native_store_wait_process(native_store *store, int pid, int options,
                              int *status_out) {
    native_process *parent = active_process(store);
    if (!parent || !status_out) return -POSIX_EFAULT;
    /* The browser Bash build passes implementation-specific wait flags. The
     * process model only has exited/stopped state, so recognized lifecycle
     * bits are ignored and unknown bits are treated as a non-blocking query. */
    if (pid < -1) return -POSIX_EINVAL;
    if (pid == 0) pid = -1; /* single process group in the browser sandbox */
    native_process *candidate = NULL;
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++) {
        native_process *child = &store->processes[i];
        if (!child->used || child->ppid != parent->pid) continue;
        if (pid > 0 && child->pid != pid) continue;
        candidate = child;
        if (child->zombie) break;
    }
    /* A browser continuation can restore the parent evaluator after the
     * child has exited while the host-side active-process selector is being
     * updated.  For wait(-1), a zombie owned by this store is still the only
     * reapable result; accept it if the parent link was transiently stale. */
    if (!candidate && pid == -1)
        for (int i = 0; i < NATIVE_PROCESS_MAX; i++)
            if (store->processes[i].used && store->processes[i].zombie) {
                candidate = &store->processes[i];
                break;
            }
    if (!candidate) return -POSIX_ECHILD;
    if (!candidate->zombie) return (options & POSIX_WNOHANG) ? 0 : -POSIX_EAGAIN;
    int child_pid = candidate->pid;
    *status_out = candidate->exit_status;
    store->last_wait_pid = child_pid;
    store->last_wait_status = candidate->exit_status;
    posix_kernel_destroy(candidate->kernel);
    memset(candidate, 0, sizeof(*candidate));
    if (store->process_count > 0) store->process_count--;
    return child_pid;
}
