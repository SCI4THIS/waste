#ifndef WASTE_TEXT_WAT_BUILDER_H
#define WASTE_TEXT_WAT_BUILDER_H

#include "text/wat_types.h"

#include <stddef.h>

typedef struct wat_context wat_context;

wat_context *wast_builder_context_create(void);
void wast_builder_context_destroy(wat_context *context);
char *wast_builder_retain_string(wat_context *context,
                                 const char *source, size_t length);
size_t wast_builder_string_length(const wat_context *context,
                                  const char *string);
void wast_builder_begin_raw(wat_context *context, wast_raw_module_kind kind);
int wast_builder_append_raw_string(wat_context *context, const char *string);
int wast_builder_take_raw(wat_context *context, wast_raw_module_kind kind,
                          wast_raw_module *raw);
int wast_builder_decode_string(wat_context *context, const char *string,
                               uint8_t *destination, size_t *length,
                               size_t capacity);

#endif /* WASTE_TEXT_WAT_BUILDER_H */
