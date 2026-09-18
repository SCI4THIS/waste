/* posix-select-abi.c — Native unit tests for fd_set decode/encode,
 * timeval/timespec validation, and select ABI utilities.
 * Built with ASan/UBSan, linked against system libc. */

#include "lib/include/select.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;
static int tests = 0;

#define CHECK(cond, ...) do { \
    tests++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
        failures++; \
    } \
} while (0)

/* --- fd_set bit operations --- */

static void test_fd_set_operations(void) {
    posix_fd_set set;
    posix_fd_zero(&set);

    /* Empty set */
    for (int i = 0; i < POSIX_FD_SETSIZE; i++)
        CHECK(!posix_fd_isset(i, &set), "fd %d set in empty set", i);
    CHECK(posix_fd_count(&set, POSIX_FD_SETSIZE) == 0, "empty count != 0");

    /* Set a few bits */
    posix_fd_set_bit(0, &set);
    posix_fd_set_bit(31, &set);
    posix_fd_set_bit(32, &set);
    posix_fd_set_bit(1023, &set);

    CHECK(posix_fd_isset(0, &set), "fd 0 not set");
    CHECK(posix_fd_isset(31, &set), "fd 31 not set");
    CHECK(posix_fd_isset(32, &set), "fd 32 not set");
    CHECK(posix_fd_isset(1023, &set), "fd 1023 not set");
    CHECK(!posix_fd_isset(1, &set), "fd 1 set");
    CHECK(!posix_fd_isset(33, &set), "fd 33 set");
    CHECK(posix_fd_count(&set, POSIX_FD_SETSIZE) == 4, "count != 4");
    CHECK(posix_fd_count(&set, 32) == 2, "count(32) != 2");
    CHECK(posix_fd_count(&set, 1) == 1, "count(1) != 1");
    CHECK(posix_fd_count(&set, 0) == 0, "count(0) != 0");

    /* Clear */
    posix_fd_clr(31, &set);
    CHECK(!posix_fd_isset(31, &set), "fd 31 still set after clr");
    CHECK(posix_fd_count(&set, POSIX_FD_SETSIZE) == 3, "count after clr != 3");

    /* Out of range is safe no-op */
    posix_fd_set_bit(-1, &set);
    posix_fd_set_bit(POSIX_FD_SETSIZE, &set);
    CHECK(!posix_fd_isset(-1, &set), "fd -1 set");
    CHECK(!posix_fd_isset(POSIX_FD_SETSIZE, &set), "fd 1024 set");
    posix_fd_clr(-1, &set);
    posix_fd_clr(POSIX_FD_SETSIZE, &set);
}

/* --- fd_set decode/encode round-trip --- */

static void test_fd_set_decode_encode(void) {
    uint8_t mem[POSIX_FD_SET_BYTES];
    posix_fd_set set;

    /* Zero memory decodes to zero set */
    memset(mem, 0, sizeof(mem));
    CHECK(posix_fd_set_decode(&set, mem) == 0, "decode zero failed");
    CHECK(posix_fd_count(&set, POSIX_FD_SETSIZE) == 0, "decoded zero not empty");

    /* Set fd 0 (bit 0 of word 0) = byte 0 bit 0 */
    mem[0] = 0x01;
    CHECK(posix_fd_set_decode(&set, mem) == 0, "decode fd0 failed");
    CHECK(posix_fd_isset(0, &set), "fd 0 not set after decode");
    CHECK(!posix_fd_isset(1, &set), "fd 1 set after decode");

    /* Set fd 8 (bit 8 of word 0) = byte 1 bit 0 */
    memset(mem, 0, sizeof(mem));
    mem[1] = 0x01;
    CHECK(posix_fd_set_decode(&set, mem) == 0, "decode fd8 failed");
    CHECK(posix_fd_isset(8, &set), "fd 8 not set");
    CHECK(!posix_fd_isset(0, &set), "fd 0 set");

    /* Set fd 32 (bit 0 of word 1) = byte 4 bit 0 */
    memset(mem, 0, sizeof(mem));
    mem[4] = 0x01;
    CHECK(posix_fd_set_decode(&set, mem) == 0, "decode fd32 failed");
    CHECK(posix_fd_isset(32, &set), "fd 32 not set");
    CHECK(!posix_fd_isset(31, &set), "fd 31 set");

    /* Round-trip: encode then decode */
    posix_fd_zero(&set);
    posix_fd_set_bit(3, &set);
    posix_fd_set_bit(63, &set);
    posix_fd_set_bit(512, &set);
    posix_fd_set_bit(1023, &set);
    memset(mem, 0xff, sizeof(mem));
    CHECK(posix_fd_set_encode(mem, &set) == 0, "encode failed");

    posix_fd_set decoded;
    CHECK(posix_fd_set_decode(&decoded, mem) == 0, "round-trip decode failed");
    CHECK(posix_fd_isset(3, &decoded), "fd 3 lost in round-trip");
    CHECK(posix_fd_isset(63, &decoded), "fd 63 lost in round-trip");
    CHECK(posix_fd_isset(512, &decoded), "fd 512 lost in round-trip");
    CHECK(posix_fd_isset(1023, &decoded), "fd 1023 lost in round-trip");
    CHECK(posix_fd_count(&decoded, POSIX_FD_SETSIZE) == 4,
          "round-trip count != 4");

    /* NULL arguments */
    CHECK(posix_fd_set_decode(NULL, mem) == -1, "decode NULL out");
    CHECK(posix_fd_set_decode(&set, NULL) == -1, "decode NULL mem");
    CHECK(posix_fd_set_encode(NULL, &set) == -1, "encode NULL mem");
    CHECK(posix_fd_set_encode(mem, NULL) == -1, "encode NULL set");
}

/* --- timeval decode/encode --- */

static void test_timeval(void) {
    uint8_t mem[POSIX_TIMEVAL_BYTES];
    posix_timeval tv;

    /* Zero */
    memset(mem, 0, sizeof(mem));
    CHECK(posix_timeval_decode(&tv, mem) == 0, "decode zero tv");
    CHECK(tv.tv_sec == 0 && tv.tv_usec == 0, "zero tv values");
    CHECK(posix_timeval_validate(&tv) == 0, "zero tv valid");

    /* Known value: tv_sec=5, tv_usec=123456 */
    memset(mem, 0, sizeof(mem));
    mem[0] = 5; /* tv_sec = 5 (LE64) */
    mem[8] = 0x40; mem[9] = 0xe2; mem[10] = 0x01; /* tv_usec = 123456 (LE32) */
    CHECK(posix_timeval_decode(&tv, mem) == 0, "decode known tv");
    CHECK(tv.tv_sec == 5, "tv_sec != 5, got %lld", (long long)tv.tv_sec);
    CHECK(tv.tv_usec == 123456, "tv_usec != 123456, got %d", tv.tv_usec);
    CHECK(posix_timeval_validate(&tv) == 0, "known tv valid");

    /* Round-trip */
    tv.tv_sec = 1000000;
    tv.tv_usec = 999999;
    CHECK(posix_timeval_encode(mem, &tv) == 0, "encode tv");
    posix_timeval tv2;
    CHECK(posix_timeval_decode(&tv2, mem) == 0, "round-trip decode tv");
    CHECK(tv2.tv_sec == 1000000, "round-trip tv_sec");
    CHECK(tv2.tv_usec == 999999, "round-trip tv_usec");

    /* Validation failures */
    tv.tv_sec = -1; tv.tv_usec = 0;
    CHECK(posix_timeval_validate(&tv) == -1, "negative tv_sec");
    tv.tv_sec = 0; tv.tv_usec = -1;
    CHECK(posix_timeval_validate(&tv) == -1, "negative tv_usec");
    tv.tv_sec = 0; tv.tv_usec = 1000000;
    CHECK(posix_timeval_validate(&tv) == -1, "tv_usec == 1000000");
    tv.tv_sec = 0; tv.tv_usec = 999999;
    CHECK(posix_timeval_validate(&tv) == 0, "tv_usec 999999 valid");

    /* NULL */
    CHECK(posix_timeval_decode(NULL, mem) == -1, "decode NULL tv");
    CHECK(posix_timeval_decode(&tv, NULL) == -1, "decode NULL mem");
    CHECK(posix_timeval_validate(NULL) == -1, "validate NULL tv");
}

/* --- timespec decode --- */

static void test_timespec(void) {
    uint8_t mem[POSIX_TIMESPEC_BYTES];
    posix_timespec ts;

    /* Zero */
    memset(mem, 0, sizeof(mem));
    CHECK(posix_timespec_decode(&ts, mem) == 0, "decode zero ts");
    CHECK(ts.tv_sec == 0 && ts.tv_nsec == 0, "zero ts values");
    CHECK(posix_timespec_validate(&ts) == 0, "zero ts valid");

    /* Known value: tv_sec=10, tv_nsec=500000 (0x7a120) */
    memset(mem, 0, sizeof(mem));
    mem[0] = 10;
    mem[8] = 0x20; mem[9] = 0xa1; mem[10] = 0x07;
    CHECK(posix_timespec_decode(&ts, mem) == 0, "decode known ts");
    CHECK(ts.tv_sec == 10, "ts_sec != 10");
    CHECK(ts.tv_nsec == 500000, "ts_nsec != 500000, got %d", ts.tv_nsec);
    CHECK(posix_timespec_validate(&ts) == 0, "known ts valid");

    /* Validation failures */
    ts.tv_sec = -1; ts.tv_nsec = 0;
    CHECK(posix_timespec_validate(&ts) == -1, "negative ts_sec");
    ts.tv_sec = 0; ts.tv_nsec = -1;
    CHECK(posix_timespec_validate(&ts) == -1, "negative ts_nsec");
    ts.tv_sec = 0; ts.tv_nsec = 1000000000;
    CHECK(posix_timespec_validate(&ts) == -1, "ts_nsec == 1e9");
    ts.tv_sec = 0; ts.tv_nsec = 999999999;
    CHECK(posix_timespec_validate(&ts) == 0, "ts_nsec 999999999 valid");

    CHECK(posix_timespec_decode(NULL, mem) == -1, "decode NULL ts");
    CHECK(posix_timespec_validate(NULL) == -1, "validate NULL ts");
}

/* --- nfds validation --- */

static void test_nfds(void) {
    CHECK(posix_nfds_validate(0) == 0, "nfds 0 valid");
    CHECK(posix_nfds_validate(1) == 0, "nfds 1 valid");
    CHECK(posix_nfds_validate(POSIX_FD_SETSIZE) == 0, "nfds 1024 valid");
    CHECK(posix_nfds_validate(-1) == -1, "nfds -1 invalid");
    CHECK(posix_nfds_validate(POSIX_FD_SETSIZE + 1) == -1, "nfds 1025 invalid");
}

int main(void) {
    test_fd_set_operations();
    test_fd_set_decode_encode();
    test_timeval();
    test_timespec();
    test_nfds();

    if (failures)
        fprintf(stderr, "%d/%d tests FAILED\n", failures, tests);
    else
        printf("posix-select-abi: %d tests passed\n", tests);
    return failures ? 1 : 0;
}
