#ifndef WASTE_SOURCE_H
#define WASTE_SOURCE_H

#include <stddef.h>
#include "../config.h"

typedef enum {
    WASTE_SOURCE_OK = 0,
    WASTE_SOURCE_NO_SHEBANG = 1,
    WASTE_SOURCE_INVALID_SHEBANG = -1,
    WASTE_SOURCE_SHEBANG_TOO_LONG = -2
} waste_source_result;

typedef struct {
    const char *body;
    size_t body_length;
    size_t source_offset;
    unsigned line_offset;
    int has_shebang;
    char interpreter[256];
    char argument[256];
} waste_source_view;

/* Identify and remove one interpreter line at byte offset zero. The returned
 * body is a borrowed view of the input; no source bytes are copied. */
waste_source_result waste_source_view_init(const char *source, size_t length,
                                           waste_source_view *view);

#endif
