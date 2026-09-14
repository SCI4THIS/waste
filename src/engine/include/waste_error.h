#ifndef WASTE_ERROR_H
#define WASTE_ERROR_H

#include <stddef.h>

typedef enum {
    WASTE_OK = 0,
    WASTE_ERROR_MALFORMED,
    WASTE_ERROR_UNSUPPORTED,
    WASTE_ERROR_TRAP,
    WASTE_ERROR_EXCEPTION,
    WASTE_ERROR_NOT_FOUND,
    WASTE_ERROR_EXIT,
    WASTE_YIELD
} waste_status;

typedef struct {
    waste_status status;
    size_t offset;
    char message[256];
} waste_error;

#endif /* WASTE_ERROR_H */
