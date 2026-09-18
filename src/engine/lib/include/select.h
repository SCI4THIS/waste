#ifndef WASTE_POSIX_SELECT_H
#define WASTE_POSIX_SELECT_H

#include <stdint.h>
#include <stddef.h>

/* Guest ABI constants — must match src/html-rt/lib/include/helper.h */
#define POSIX_FD_SETSIZE  1024
#define POSIX_NFDBITS     32
#define POSIX_FD_SET_WORDS (POSIX_FD_SETSIZE / POSIX_NFDBITS)
#define POSIX_FD_SET_BYTES (POSIX_FD_SET_WORDS * 4)

/* Guest struct timeval: { i64 tv_sec @ 0, i32 tv_usec @ 8, pad[4] } = 16 */
#define POSIX_TIMEVAL_BYTES 16

/* Guest struct timespec: { i64 tv_sec @ 0, i32 tv_nsec @ 8, pad[4] } = 16 */
#define POSIX_TIMESPEC_BYTES 16

/* Guest sigset_t: { u32 sig[4] } = 16 */
#define POSIX_SIGSET_BYTES 16

/* Host-side fd_set: one bit per fd, stored as an array of 32-bit words
 * matching the guest layout so decode/encode can operate word-at-a-time. */
typedef struct {
    uint32_t words[POSIX_FD_SET_WORDS];
} posix_fd_set;

/* Host-side timeval (widened for the host). */
typedef struct {
    int64_t tv_sec;
    int32_t tv_usec;
} posix_timeval;

/* Host-side timespec (widened for the host). */
typedef struct {
    int64_t tv_sec;
    int32_t tv_nsec;
} posix_timespec;

/* --- fd_set bit operations --- */

static inline void posix_fd_zero(posix_fd_set *set) {
    for (int i = 0; i < POSIX_FD_SET_WORDS; i++) set->words[i] = 0;
}

static inline int posix_fd_isset(int fd, const posix_fd_set *set) {
    if (fd < 0 || fd >= POSIX_FD_SETSIZE) return 0;
    return (set->words[fd / POSIX_NFDBITS] >> (fd % POSIX_NFDBITS)) & 1;
}

static inline void posix_fd_set_bit(int fd, posix_fd_set *set) {
    if (fd < 0 || fd >= POSIX_FD_SETSIZE) return;
    set->words[fd / POSIX_NFDBITS] |= (uint32_t)1 << (fd % POSIX_NFDBITS);
}

static inline void posix_fd_clr(int fd, posix_fd_set *set) {
    if (fd < 0 || fd >= POSIX_FD_SETSIZE) return;
    set->words[fd / POSIX_NFDBITS] &= ~((uint32_t)1 << (fd % POSIX_NFDBITS));
}

/* Count total set bits up to (but not including) nfds. */
int posix_fd_count(const posix_fd_set *set, int nfds);

/* --- Guest memory decode/encode --- */

/* Decode an fd_set from guest memory bytes (little-endian u32 words).
 * mem must point to at least POSIX_FD_SET_BYTES readable bytes.
 * Returns 0 on success, -1 if mem is NULL. */
int posix_fd_set_decode(posix_fd_set *out, const uint8_t *mem);

/* Encode an fd_set back to guest memory bytes (little-endian u32 words).
 * mem must point to at least POSIX_FD_SET_BYTES writable bytes.
 * Returns 0 on success, -1 if mem is NULL. */
int posix_fd_set_encode(uint8_t *mem, const posix_fd_set *set);

/* Decode a timeval from guest memory (little-endian).
 * mem must point to at least POSIX_TIMEVAL_BYTES readable bytes.
 * Returns 0 on success, -1 if mem is NULL. */
int posix_timeval_decode(posix_timeval *out, const uint8_t *mem);

/* Encode a timeval to guest memory (little-endian).
 * mem must point to at least POSIX_TIMEVAL_BYTES writable bytes.
 * Returns 0 on success, -1 if mem is NULL. */
int posix_timeval_encode(uint8_t *mem, const posix_timeval *tv);

/* Decode a timespec from guest memory (little-endian).
 * mem must point to at least POSIX_TIMESPEC_BYTES readable bytes.
 * Returns 0 on success, -1 if mem is NULL. */
int posix_timespec_decode(posix_timespec *out, const uint8_t *mem);

/* --- Validation --- */

/* Validate timeval fields: tv_sec >= 0, 0 <= tv_usec < 1000000.
 * Returns 0 if valid, -1 (EINVAL) if not. */
int posix_timeval_validate(const posix_timeval *tv);

/* Validate timespec fields: tv_sec >= 0, 0 <= tv_nsec < 1000000000.
 * Returns 0 if valid, -1 (EINVAL) if not. */
int posix_timespec_validate(const posix_timespec *ts);

/* Validate nfds: must be >= 0 and <= FD_SETSIZE.
 * Returns 0 if valid, -1 (EINVAL) if not. */
int posix_nfds_validate(int nfds);

#endif /* WASTE_POSIX_SELECT_H */
