/* Scenario/observation input only. All result assertions are in abort.wast. */
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../../aux/libc-imports.h"

extern __typeof__(abort) abort __attribute__((import_module("libc")));
__attribute__((import_module("env"), import_name("abort")))
extern void legacy_abort(void);

static int marker_fd;
static int recursive;

static void caught(int signal) {
    sigset_t mask;
    char observed = (char)signal;
    write(marker_fd, &observed, 1);
    if (recursive) abort();
    /* Returning after reblocking and ignoring must still terminate. */
    struct sigaction action = { .sa_handler = SIG_IGN };
    sigemptyset(&action.sa_mask);
    sigaction(SIGABRT, &action, 0);
    sigemptyset(&mask);
    sigaddset(&mask, SIGABRT);
    sigprocmask(SIG_BLOCK, &mask, 0);
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;
    for (int mode = 0; mode < 6; mode++) {
        int descriptors[2];
        int pipe_result = pipe(descriptors);
        int child = pipe_result ? -1 : fork();
        if (child == 0) {
            close(descriptors[0]);
            marker_fd = descriptors[1];
            recursive = mode == 4;
            struct sigaction action = { .sa_handler = SIG_DFL };
            sigemptyset(&action.sa_mask);
            if (mode == 1) action.sa_handler = SIG_IGN;
            if (mode == 3 || mode == 4) action.sa_handler = caught;
            sigaction(SIGABRT, &action, 0);
            if (mode >= 2) {
                sigset_t mask;
                sigemptyset(&mask);
                sigaddset(&mask, SIGABRT);
                sigprocmask(SIG_BLOCK, &mask, 0);
            }
            if (mode == 5) {
                legacy_abort();
                _exit(99);
            }
            abort();
        }
        int status = -1;
        int waited = -1;
        unsigned char marker = 0;
        if (!pipe_result) {
            close(descriptors[1]);
            if (child > 0) waited = waitpid(child, &status, 0);
            read(descriptors[0], &marker, 1);
            close(descriptors[0]);
        }
        printf("%d %d %d %d\n", mode, waited == child && child > 0,
               status, (int)marker);
    }
    printf("parent-alive\n");
    return 0;
}
