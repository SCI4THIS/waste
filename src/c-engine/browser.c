#include "waste_tail.h"

#include <stddef.h>
#include <stdint.h>

extern unsigned char __heap_base;

static uintptr_t heap_cursor;
static waste_module *loaded_module;
static waste_error last_error;
static waste_run_stats last_stats;
static int64_t last_result;

void *memcpy(void *destination, const void *source, size_t count) {
  unsigned char *to = destination;
  const unsigned char *from = source;
  for (size_t index = 0; index < count; ++index) to[index] = from[index];
  return destination;
}

int memcmp(const void *left, const void *right, size_t count) {
  const unsigned char *a = left;
  const unsigned char *b = right;
  for (size_t index = 0; index < count; ++index) {
    if (a[index] != b[index]) return a[index] < b[index] ? -1 : 1;
  }
  return 0;
}

int strcmp(const char *left, const char *right) {
  while (*left != '\0' && *left == *right) {
    ++left;
    ++right;
  }
  return (unsigned char)*left - (unsigned char)*right;
}

void *malloc(size_t size) {
  uintptr_t start;
  uintptr_t limit;
  uintptr_t memory_size;
  uint32_t pages;
  if (heap_cursor == 0) heap_cursor = (uintptr_t)&__heap_base;
  start = (heap_cursor + 15u) & ~(uintptr_t)15u;
  if (size > UINTPTR_MAX - start) return NULL;
  limit = start + size;
  memory_size = (uintptr_t)__builtin_wasm_memory_size(0) * 65536u;
  if (limit > memory_size) {
    uintptr_t missing = limit - memory_size;
    pages = (uint32_t)((missing + 65535u) / 65536u);
    if (__builtin_wasm_memory_grow(0, pages) == (size_t)-1) return NULL;
  }
  heap_cursor = limit;
  return (void *)start;
}

void *calloc(size_t count, size_t size) {
  unsigned char *result;
  size_t total;
  if (count != 0 && size > SIZE_MAX / count) return NULL;
  total = count * size;
  result = malloc(total);
  if (result == NULL) return NULL;
  for (size_t index = 0; index < total; ++index) result[index] = 0;
  return result;
}

void free(void *pointer) {
  (void)pointer;
}

__attribute__((export_name("waste_poc_alloc")))
uint32_t waste_poc_alloc(uint32_t size) {
  return (uint32_t)(uintptr_t)malloc(size);
}

__attribute__((export_name("waste_poc_load")))
uint32_t waste_poc_load(uint32_t address, uint32_t size) {
  loaded_module = NULL;
  return (uint32_t)waste_module_load(
    (const uint8_t *)(uintptr_t)address, size, &loaded_module, &last_error);
}

__attribute__((export_name("waste_poc_run")))
uint32_t waste_poc_run(uint32_t mode, int64_t iterations) {
  uint32_t function_index;
  waste_status status;
  const char *name = mode == 0 ? "direct" : "reference";
  if (loaded_module == NULL || iterations < 0 ||
      (uint64_t)iterations > (UINT64_MAX - 1024) / 8) return WASTE_ERROR_FORMAT;
  status = waste_module_find_export(loaded_module, name, &function_index, &last_error);
  if (status != WASTE_OK) return (uint32_t)status;
  status = waste_run_i64(loaded_module, function_index, iterations,
    (uint64_t)iterations * 8 + 1024, &last_result, &last_stats, &last_error);
  return (uint32_t)status;
}

__attribute__((export_name("waste_poc_result")))
int64_t waste_poc_result(void) { return last_result; }

__attribute__((export_name("waste_poc_tail_calls")))
int64_t waste_poc_tail_calls(void) { return (int64_t)last_stats.tail_calls; }

__attribute__((export_name("waste_poc_instructions")))
int64_t waste_poc_instructions(void) { return (int64_t)last_stats.instructions; }

__attribute__((export_name("waste_poc_max_frames")))
uint32_t waste_poc_max_frames(void) { return last_stats.maximum_frame_depth; }

__attribute__((export_name("waste_poc_run_allocations")))
int64_t waste_poc_run_allocations(void) {
  return (int64_t)last_stats.allocations_during_run;
}
