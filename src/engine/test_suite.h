#ifndef WASTE_TEST_SUITE_H
#define WASTE_TEST_SUITE_H

#include <stddef.h>
#include <stdint.h>

#define WASTE_SUITE_MAX_TESTS 4096u
#define WASTE_SUITE_MAX_ASSETS 16u
#define WASTE_SUITE_MAX_BYTES (64u * 1024u * 1024u)
#define WASTE_SUITE_TEST_PREFIX "/root/waste/tests/"
#define WASTE_SUITE_SUPPORT_PREFIX "/root/waste/tests/.support/"
#define WASTE_SUITE_MANIFEST "/root/waste/tests/manifest.json"

typedef struct {
    char path[1024], mount_path[1024];
    uint32_t mode;
} waste_suite_asset;

typedef struct {
    char identity[512], group[128], path[1024], file[512], mode[64];
    char skip_reason[256];
    int unsupported, expect_failure;
    waste_suite_asset *assets;
    unsigned asset_count;
} waste_suite_test;

typedef struct {
    waste_suite_test *tests;
    unsigned count;
} waste_suite;

/* Decode the mounted /root/waste/tests/manifest.json format 1 without host I/O. Owns
 * its test records; borrowed JSON may be released after a successful decode.
 * Unknown provenance/spec fields are validated as JSON but never executed. */
int waste_suite_decode(const char *bytes, size_t length, waste_suite *suite,
                        char *error, size_t error_length);
void waste_suite_free(waste_suite *suite);

#endif
