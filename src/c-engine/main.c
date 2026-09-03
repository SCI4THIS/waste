#define _POSIX_C_SOURCE 200809L

#include "waste_tail.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int read_file(const char *path, uint8_t **bytes, size_t *size) {
  FILE *file = fopen(path, "rb");
  long length;
  if (file == NULL) {
    fprintf(stderr, "%s: %s\n", path, strerror(errno));
    return 1;
  }
  if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
      fseek(file, 0, SEEK_SET) != 0) {
    fprintf(stderr, "%s: unable to determine file size\n", path);
    fclose(file);
    return 1;
  }
  *bytes = malloc((size_t)length);
  if (*bytes == NULL || fread(*bytes, 1, (size_t)length, file) != (size_t)length) {
    fprintf(stderr, "%s: unable to read module\n", path);
    free(*bytes);
    fclose(file);
    return 1;
  }
  fclose(file);
  *size = (size_t)length;
  return 0;
}

static uint64_t elapsed_nanoseconds(struct timespec start, struct timespec finish) {
  uint64_t seconds = (uint64_t)(finish.tv_sec - start.tv_sec);
  int64_t nanoseconds = finish.tv_nsec - start.tv_nsec;
  if (nanoseconds < 0) {
    --seconds;
    nanoseconds += 1000000000;
  }
  return seconds * UINT64_C(1000000000) + (uint64_t)nanoseconds;
}

int main(int argc, char **argv) {
  uint8_t *bytes = NULL;
  size_t size = 0;
  waste_module *module = NULL;
  waste_error error = {0};
  waste_status status;
  uint32_t function_index;
  uint64_t iterations;
  uint64_t fuel;
  int64_t result;
  waste_run_stats stats;
  struct timespec started;
  struct timespec finished;
  uint64_t elapsed;
  char *iteration_end = NULL;

  if (argc != 4) {
    fprintf(stderr, "usage: %s MODULE.wasm EXPORT ITERATIONS\n", argv[0]);
    return 2;
  }
  errno = 0;
  iterations = strtoull(argv[3], &iteration_end, 10);
  if (errno != 0 || iteration_end == argv[3] || *iteration_end != '\0' ||
      argv[3][0] == '-' || iterations > (UINT64_MAX - 1024) / 8) {
    fprintf(stderr, "invalid iteration count: %s\n", argv[3]);
    return 2;
  }
  if (read_file(argv[1], &bytes, &size) != 0) {
    return 2;
  }
  status = waste_module_load(bytes, size, &module, &error);
  free(bytes);
  if (status != WASTE_OK) {
    fprintf(stderr, "load %s at byte %zu: %s\n",
      waste_status_name(status), error.offset, error.message);
    return 3;
  }
  status = waste_module_find_export(module, argv[2], &function_index, &error);
  if (status != WASTE_OK) {
    fprintf(stderr, "export %s: %s\n", waste_status_name(status), error.message);
    waste_module_free(module);
    return 3;
  }
  fuel = iterations * 8 + 1024;
  (void)clock_gettime(CLOCK_MONOTONIC, &started);
  status = waste_run_i64(module, function_index, (int64_t)iterations,
    fuel, &result, &stats, &error);
  (void)clock_gettime(CLOCK_MONOTONIC, &finished);
  elapsed = elapsed_nanoseconds(started, finished);
  if (status != WASTE_OK) {
    fprintf(stderr, "run %s at pc %zu: %s\n",
      waste_status_name(status), error.offset, error.message);
    waste_module_free(module);
    return 4;
  }
  printf(
    "export=%s argument=%" PRIu64 " result=%" PRId64
    " instructions=%" PRIu64 " tail_calls=%" PRIu64
    " max_frames=%" PRIu32 " run_allocations=%" PRIu64
    " elapsed_ns=%" PRIu64 " tail_calls_per_second=%.0f\n",
    argv[2], iterations, result, stats.instructions, stats.tail_calls,
    stats.maximum_frame_depth, stats.allocations_during_run, elapsed,
    elapsed == 0 ? 0.0 : (double)stats.tail_calls * 1000000000.0 / (double)elapsed
  );
  waste_module_free(module);
  return result == 0 && stats.tail_calls == iterations &&
    stats.maximum_frame_depth == 1 && stats.allocations_during_run == 0 ? 0 : 5;
}
