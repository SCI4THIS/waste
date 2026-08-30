#ifndef FILEGUARD_EXECUTE_H__
#define FILEGUARD_EXECUTE_H__

#include <stdint.h>
#include <stddef.h>

const char *execute_wasm(const uint8_t *buf, size_t len, int argc, char **argv);
const char *execute_wast(const uint8_t *buf, size_t len, int argc, char **argv);

#endif
