#ifndef WASTE_TAIL_H
#define WASTE_TAIL_H

#include <stddef.h>
#include <stdint.h>

typedef struct waste_module waste_module;

typedef enum {
  WASTE_OK = 0,
  WASTE_ERROR_FORMAT,
  WASTE_ERROR_UNSUPPORTED,
  WASTE_ERROR_LIMIT,
  WASTE_ERROR_NOT_FOUND,
  WASTE_ERROR_TRAP,
  WASTE_ERROR_FUEL
} waste_status;

typedef struct {
  waste_status status;
  size_t offset;
  char message[160];
} waste_error;

typedef struct {
  uint64_t instructions;
  uint64_t tail_calls;
  uint32_t maximum_frame_depth;
  uint64_t allocations_during_run;
} waste_run_stats;

waste_status waste_module_load(
  const uint8_t *bytes,
  size_t size,
  waste_module **module,
  waste_error *error
);

void waste_module_free(waste_module *module);

waste_status waste_module_find_export(
  const waste_module *module,
  const char *name,
  uint32_t *function_index,
  waste_error *error
);

waste_status waste_run_i64(
  const waste_module *module,
  uint32_t function_index,
  int64_t argument,
  uint64_t fuel,
  int64_t *result,
  waste_run_stats *stats,
  waste_error *error
);

const char *waste_status_name(waste_status status);

#endif
