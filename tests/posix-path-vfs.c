#include "include/kernel.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;
#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { fprintf(stderr, "path VFS check failed: %s\n", #condition); failures++; } \
} while (0)

int main(void) {
    posix_kernel *first = posix_kernel_create(0);
    posix_kernel *second = posix_kernel_create(0);
    CHECK(first != NULL && second != NULL);
    if (!first || !second) return 1;

    const posix_path_metadata regular = { POSIX_NODE_REGULAR, 0755, 0, 0, 12, 3 };
    const posix_path_metadata directory = { POSIX_NODE_DIRECTORY, 0755, 0, 0, 0, 6 };
    const posix_path_metadata readonly = { POSIX_NODE_REGULAR, 0444, 0, 0, 1, 4 };
    const posix_path_metadata inaccessible = { POSIX_NODE_REGULAR, 0000, 0, 0, 1, 5 };
    CHECK(posix_kernel_path_add(first, "/bin/tool", &regular) == 0);
    CHECK(posix_kernel_path_add(first, "/data", &directory) == 0);
    CHECK(posix_kernel_path_add(first, "/data/readme", &readonly) == 0);
    CHECK(posix_kernel_path_add(first, "/data/secret", &inaccessible) == 0);

    posix_path_metadata actual;
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"/bin/tool", 9, 1, &actual) == 0);
    CHECK(actual.size == 12 && actual.kind == POSIX_NODE_REGULAR);
    CHECK(posix_kernel_path_access(first, (const uint8_t *)"/bin/tool", 9, POSIX_X_OK, 0) == 0);
    CHECK(posix_kernel_path_access(first, (const uint8_t *)"/data/readme", 12, POSIX_W_OK, 0) == -POSIX_EACCES);
    CHECK(posix_kernel_path_access(first, (const uint8_t *)"/data/secret", 12, POSIX_F_OK, 0) == 0);
    CHECK(posix_kernel_path_access(first, (const uint8_t *)"/missing", 8, POSIX_F_OK, 0) == -POSIX_ENOENT);
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"/bin/tool/child", 15, 1, &actual) == -POSIX_ENOTDIR);

    CHECK(posix_kernel_path_set_cwd(first, "/data") == 0);
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"././readme", 10, 1, &actual) == 0);
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"../bin/../bin/tool", sizeof("../bin/../bin/tool") - 1, 1, &actual) == 0);
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"../../../../bin/tool", 20, 1, &actual) == 0);
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"//bin///tool", 12, 1, &actual) == 0);
    CHECK(posix_kernel_path_set_cwd(first, "/data/readme") == -POSIX_ENOTDIR);
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"", 0, 1, &actual) == -POSIX_EINVAL);
    CHECK(posix_kernel_path_access(first, (const uint8_t *)"/bin/tool", 9, 8, 0) == -POSIX_EINVAL);

    CHECK(posix_kernel_path_stat(second, (const uint8_t *)"/bin/tool", 9, 1, &actual) == -POSIX_ENOENT);
    CHECK(posix_kernel_path_add(second, "/bin/tool", &regular) == 0);
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)"/bin/tool", 9, 1, &actual) == 0);

    char overlong[POSIX_PATH_MAX];
    memset(overlong, 'x', sizeof(overlong));
    CHECK(posix_kernel_path_stat(first, (const uint8_t *)overlong,
                                 sizeof(overlong), 1, &actual) == -POSIX_EINVAL);
    posix_kernel_destroy(first);
    posix_kernel_destroy(second);
    printf("POSIX path VFS: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
