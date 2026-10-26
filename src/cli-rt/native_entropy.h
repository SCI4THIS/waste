#ifndef WASTE_NATIVE_ENTROPY_H
#define WASTE_NATIVE_ENTROPY_H
#include <stdint.h>
#include <errno.h>
#include <sys/random.h>

static int32_t native_entropy(void *data, void *bytes, uint32_t length) {
    (void)data;
    ssize_t result;
    do { result = getrandom(bytes, length, 0); } while (result < 0 && errno == EINTR);
    return result < 0 ? -5 : (int32_t)result;
}
#endif
