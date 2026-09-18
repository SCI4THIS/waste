#include "include/select.h"

/* --- fd_set bit counting --- */

int posix_fd_count(const posix_fd_set *set, int nfds) {
    if (!set || nfds <= 0) return 0;
    if (nfds > POSIX_FD_SETSIZE) nfds = POSIX_FD_SETSIZE;
    int count = 0;
    int full_words = nfds / POSIX_NFDBITS;
    int remaining = nfds % POSIX_NFDBITS;
    for (int i = 0; i < full_words; i++) {
        uint32_t w = set->words[i];
        while (w) { count++; w &= w - 1; }
    }
    if (remaining > 0 && full_words < POSIX_FD_SET_WORDS) {
        uint32_t mask = ((uint32_t)1 << remaining) - 1;
        uint32_t w = set->words[full_words] & mask;
        while (w) { count++; w &= w - 1; }
    }
    return count;
}

/* --- Little-endian helpers --- */

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int64_t read_le64(const uint8_t *p) {
    uint64_t lo = read_le32(p);
    uint64_t hi = read_le32(p + 4);
    return (int64_t)(lo | (hi << 32));
}

static void write_le64(uint8_t *p, int64_t v) {
    write_le32(p, (uint32_t)(uint64_t)v);
    write_le32(p + 4, (uint32_t)((uint64_t)v >> 32));
}

/* --- fd_set decode/encode --- */

int posix_fd_set_decode(posix_fd_set *out, const uint8_t *mem) {
    if (!out || !mem) return -1;
    for (int i = 0; i < POSIX_FD_SET_WORDS; i++)
        out->words[i] = read_le32(mem + i * 4);
    return 0;
}

int posix_fd_set_encode(uint8_t *mem, const posix_fd_set *set) {
    if (!mem || !set) return -1;
    for (int i = 0; i < POSIX_FD_SET_WORDS; i++)
        write_le32(mem + i * 4, set->words[i]);
    return 0;
}

/* --- timeval decode/encode --- */

int posix_timeval_decode(posix_timeval *out, const uint8_t *mem) {
    if (!out || !mem) return -1;
    out->tv_sec = read_le64(mem);
    out->tv_usec = (int32_t)read_le32(mem + 8);
    return 0;
}

int posix_timeval_encode(uint8_t *mem, const posix_timeval *tv) {
    if (!mem || !tv) return -1;
    write_le64(mem, tv->tv_sec);
    write_le32(mem + 8, (uint32_t)tv->tv_usec);
    write_le32(mem + 12, 0); /* padding */
    return 0;
}

/* --- timespec decode --- */

int posix_timespec_decode(posix_timespec *out, const uint8_t *mem) {
    if (!out || !mem) return -1;
    out->tv_sec = read_le64(mem);
    out->tv_nsec = (int32_t)read_le32(mem + 8);
    return 0;
}

/* --- Validation --- */

int posix_timeval_validate(const posix_timeval *tv) {
    if (!tv) return -1;
    if (tv->tv_sec < 0 || tv->tv_usec < 0 || tv->tv_usec >= 1000000)
        return -1;
    return 0;
}

int posix_timespec_validate(const posix_timespec *ts) {
    if (!ts) return -1;
    if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000)
        return -1;
    return 0;
}

int posix_nfds_validate(int nfds) {
    if (nfds < 0 || nfds > POSIX_FD_SETSIZE)
        return -1;
    return 0;
}
