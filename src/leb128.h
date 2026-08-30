#ifndef FILEGUARD_LEB128_H__
#define FILEGUARD_LEB128_H__

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

bool leb_u  (size_t N, const uint8_t **buf, size_t len, uint8_t *val, size_t val_size);
bool leb_s  (size_t N, const uint8_t **buf, size_t len, uint8_t *val, size_t val_size);
bool leb_u32(const uint8_t **buf, size_t len, uint32_t *val);
bool leb_i32(const uint8_t **buf, size_t len, int32_t  *val);
bool leb_i64(const uint8_t **buf, size_t len, int64_t  *val);
bool leb_u64(const uint8_t **buf, size_t len, uint64_t *val);

#endif
