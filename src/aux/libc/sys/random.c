/* Cryptographic entropy comes from the runtime capability, never rand(). */
#include "../include/helper.h"
__attribute__((import_module("waste_kernel"), import_name("getrandom_v1")))
extern i32 waste_kernel_getrandom_v1(void *, u32, u32);

i32 getrandom(void *buffer, u32 count, u32 flags) {
    i32 result = waste_kernel_getrandom_v1(buffer, count, flags);
    if (result < 0) { *__errno_location() = -result; return -1; }
    return result;
}

i32 getentropy(void *buffer, u32 count) {
    if (count > 256) { *__errno_location() = 5; return -1; }
    u32 at = 0;
    while (at < count) {
        i32 n = getrandom((unsigned char *)buffer + at, count - at, 0);
        if (n <= 0) return -1;
        at += (u32)n;
    }
    return 0;
}
