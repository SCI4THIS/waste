#ifndef WASTE_STREAM_H
#define WASTE_STREAM_H

#include "wast_types.h"

#include <stddef.h>

typedef enum {
    WAST_STREAM_MODULE,
    WAST_STREAM_ASSERTION,
    WAST_STREAM_REGISTER,
    WAST_STREAM_INVOKE,
    WAST_STREAM_UNKNOWN
} wast_stream_command_kind;

typedef struct {
    const char *source;
    size_t length;
    size_t offset;
    unsigned line;
    unsigned column;
    char error[256];
} wast_stream;

typedef int (*wast_stream_callback)(wast_stream_command_kind kind,
                                    const char *bytes, size_t length,
                                    size_t offset, unsigned line,
                                    wast_script *parsed, void *opaque);

void wast_stream_init(wast_stream *stream, const char *source, size_t length);

/* Consume one balanced top-level WAST command. Returns 1 when a command was
 * delivered, 0 at end of input, and -1 for an unterminated command. */
int wast_stream_next(wast_stream *stream, wast_stream_callback callback,
                     void *opaque);

/* Parse and dispatch every command, recovering at command boundaries. Returns
 * the number of commands delivered, or -1 for an invalid argument. */
int wast_stream_run(const char *source, size_t length,
                    wast_stream_callback callback, void *opaque);

#endif
