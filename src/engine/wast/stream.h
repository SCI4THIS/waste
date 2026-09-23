#ifndef WASTE_STREAM_H
#define WASTE_STREAM_H

#include "wat/types.h"
#include "wast/command.h"

#include <stddef.h>

typedef struct {
    const char *source;
    size_t length;
    size_t source_offset;
    unsigned line_offset;
    size_t offset;
    unsigned line;
    unsigned column;
    void *context;
    void *scanner;
    void *buffer;
    int initialized;
    int finished;
    char error[256];
} wast_stream;

typedef int (*wast_stream_callback)(wast_stream_command_kind kind,
                                    const char *bytes, size_t length,
                                    size_t offset, unsigned line,
                                    wast_script *parsed, void *opaque);

void wast_stream_init(wast_stream *stream, const char *source, size_t length);
void wast_stream_destroy(wast_stream *stream);

/* Return the current cursor in the original source coordinate space. The
 * scanner's internal offset is relative to the shebang-stripped body. */
int wast_stream_position(const wast_stream *stream, size_t *offset_out,
                         unsigned *line_out);

/* Consume one balanced top-level WAST command. Returns 1 when a command was
 * delivered, 0 at end of input, and -1 for an unterminated command. */
int wast_stream_next(wast_stream *stream, wast_stream_callback callback,
                     void *opaque);

/* Parse and dispatch every command, recovering at command boundaries. Returns
 * the number of commands delivered, or -1 for an invalid argument. */
int wast_stream_run(const char *source, size_t length,
                    wast_stream_callback callback, void *opaque);

#endif
