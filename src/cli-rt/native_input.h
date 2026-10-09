#ifndef WASTE_NATIVE_INPUT_H
#define WASTE_NATIVE_INPUT_H
#include "native_runtime.h"
#include <limits.h>

typedef struct {
    char root[PATH_MAX];
    char guest[NATIVE_EXEC_PATH_MAX];
    uint8_t *bytes;
    size_t size;
    int staged;
    unsigned mode;
} native_application_input;
int native_input_resolve_root(const char *, char *, size_t);
int native_application_input_open(native_application_input *, const char *root,
    const char *file, native_exec_format, char *error, size_t capacity);
void native_application_input_destroy(native_application_input *);
#endif
