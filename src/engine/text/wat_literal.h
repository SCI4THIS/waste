#ifndef WASTE_WAST_LITERAL_H
#define WASTE_WAST_LITERAL_H

#include "text/wat_context.h"

#include <stdint.h>

const char *wast_literal_strip_underscores(wat_context *context,
                                           const char *source);
uint64_t wast_literal_parse_hex_payload(const char *source);
int wast_literal_integer_is_valid(const char *source, size_t length, int bits);
int wast_literal_float_is_valid(wat_context *context, const char *source,
                                size_t length, int bits);
double wast_literal_parse_float(wat_context *context, const char *source,
                                int bits);

#endif
