#include <sys/mman.h>

extern int waste_kernel_posix_madvise_v1(void *, size_t, int)
    __attribute__((import_module("waste_kernel"),
                   import_name("posix_madvise_v1")));

int posix_madvise(void *address, size_t length, int advice) {
    return waste_kernel_posix_madvise_v1(address, length, advice);
}
