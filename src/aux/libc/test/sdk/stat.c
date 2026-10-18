/* The adapter writes 128 bytes; the old 112-byte public declaration let it
 * overwrite application data. Exercise the real compiled browser adapter. */
#include <stdint.h>
#include <sys/stat.h>
#include <signal.h>

int sdk_stat_check(void) {
    struct {
        struct stat metadata;
        uint32_t canary[4];
    } value;
    for (int i = 0; i < 4; ++i) value.canary[i] = UINT32_C(0xdeadbeef);
    if (stat("/usr/include/stdio.h", &value.metadata) != 0) return 0;
    if (!S_ISREG(value.metadata.st_mode) || value.metadata.st_size <= 0) return 0;
    for (int i = 0; i < 4; ++i) if (value.canary[i] != UINT32_C(0xdeadbeef)) return 0;
    return 1;
}

/* Compile against the mounted public layout, then execute real imports. */
int sdk_signal_check(void) {
    struct {
        sigset_t set;
        uint32_t canary;
    } value;
    sigset_t saved, current;
    value.canary = UINT32_C(0xdeadbeef);
    if (sigemptyset(&value.set) || sigaddset(&value.set, SIGINT) ||
        sigaddset(&value.set, 128) || sigismember(&value.set, 128) != 1)
        return 0;
    if (value.set.__bits[0] != 2 || value.set.__bits[3] != UINT32_C(0x80000000))
        return 0;
    if (sigprocmask(SIG_BLOCK, &value.set, &saved)) return 0;
    if (sigprocmask(SIG_SETMASK, 0, &current)) return 0;
    int ok = (current.__bits[0] & 2) && (current.__bits[3] & UINT32_C(0x80000000));
    if (sigprocmask(SIG_SETMASK, &saved, 0)) return 0;
    if (sigpending(&value.set)) return 0;
    for (int i = 0; i < 4; ++i) if (value.set.__bits[i]) return 0;
    if (sigfillset(&value.set) ||
        sigdelset(&value.set, SIGINT) || sigismember(&value.set, SIGINT) != 0)
        return 0;
    return ok && value.set.__bits[0] == UINT32_C(0xfffffffd) &&
        value.set.__bits[3] == UINT32_MAX && value.canary == UINT32_C(0xdeadbeef);
}
