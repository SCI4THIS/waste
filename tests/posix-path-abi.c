#include "include/path.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int checks;
static int failures;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { fprintf(stderr, "path ABI check failed: %s\n", #condition); failures++; } \
} while (0)

int main(void) {
    posix_path_metadata input = { POSIX_NODE_REGULAR, 0100755, 1000, 1000, 123456, 42 };
    posix_path_metadata output;
    uint8_t metadata_bytes[POSIX_PATH_METADATA_BYTES];
    CHECK(posix_path_metadata_validate(&input) == 0);
    CHECK(posix_path_metadata_encode(metadata_bytes, &input) == 0);
    CHECK(posix_path_metadata_decode(&output, metadata_bytes) == 0);
    CHECK(memcmp(&input, &output, sizeof(input)) == 0);
    CHECK(posix_path_metadata_encode(NULL, &input) < 0);
    input.kind = POSIX_NODE_NONE;
    CHECK(posix_path_metadata_validate(&input) < 0);

    posix_guest_stat stat = {0};
    stat.st_dev = 7; stat.st_ino = 9; stat.st_mode = 0100755;
    stat.st_size = 8192; stat.st_atime_sec = 11; stat.st_ctime_nsec = 13;
    uint8_t stat_bytes[POSIX_GUEST_STAT_BYTES];
    posix_guest_stat decoded;
    CHECK(posix_guest_stat_encode(stat_bytes, &stat) == 0);
    CHECK(posix_guest_stat_decode(&decoded, stat_bytes) == 0);
    CHECK(memcmp(&stat, &decoded, sizeof(stat)) == 0);
    CHECK(posix_guest_stat_decode(NULL, stat_bytes) < 0);
    CHECK(stat_bytes[16] == 0xed && stat_bytes[17] == 0x81);
    printf("POSIX path ABI: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
