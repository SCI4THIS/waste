/* M4's two command builtins use ordinary guest fork/exec/pipe/wait.
 * Gnulib's general posix_spawn implementation needs unavailable host APIs. */
#include "waste-process.h"
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

int m4_waste_wait(pid_t pid, int *signal_status) {
    int status;
    pid_t result;
    *signal_status = 0;
    do {
        result = waitpid(pid, &status, 0);
    } while (result < 0 && errno == EINTR);
    if (result < 0) return 127;
    if (WIFSIGNALED(status)) {
        *signal_status = WTERMSIG(status);
        return 127;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 127;
}

static void child_exec(const char *const args[]) {
    execv("/usr/bin/bash", (char *const *)args);
    _exit(errno == ENOENT ? 127 : 126);
}

int m4_waste_execute(const char *const args[], int *signal_status) {
    *signal_status = 0;
    if (fflush(NULL) != 0) return 127;
    pid_t child = fork();
    if (child < 0) return 127;
    if (child == 0) child_exec(args);
    return m4_waste_wait(child, signal_status);
}

pid_t m4_waste_pipe_in(const char *const args[], int *read_fd) {
    int descriptors[2];
    if (fflush(NULL) != 0 || pipe(descriptors) < 0) return -1;
    pid_t child = fork();
    if (child == 0) {
        close(descriptors[0]);
        if (descriptors[1] != STDOUT_FILENO) {
            if (dup2(descriptors[1], STDOUT_FILENO) < 0) _exit(126);
            close(descriptors[1]);
        }
        child_exec(args);
    }
    int saved_errno = errno;
    close(descriptors[1]);
    if (child < 0) {
        close(descriptors[0]);
        errno = saved_errno;
        return -1;
    }
    *read_fd = descriptors[0];
    return child;
}
