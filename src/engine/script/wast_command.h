#ifndef WASTE_SCRIPT_WAST_COMMAND_H
#define WASTE_SCRIPT_WAST_COMMAND_H

#include <stddef.h>

typedef enum {
    WAST_STREAM_MODULE,
    WAST_STREAM_ASSERTION,
    WAST_STREAM_REGISTER,
    WAST_STREAM_INVOKE,
    WAST_STREAM_UNKNOWN
} wast_stream_command_kind;

wast_stream_command_kind wast_command_classify(const char *source,
                                                size_t length);

#endif /* WASTE_SCRIPT_WAST_COMMAND_H */
