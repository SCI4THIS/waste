#include "execute.h"
#include "validate.h"
#include "lookup.h"

#define DEBUG_EXECUTE 1

#if DEBUG_EXECUTE
#include <stdio.h>
#define DEBUG(...) printf("%s: %d: ", __FILE__, __LINE__); printf(__VA_ARGS__)
#else
#define DEBUG(...) { }
#endif

#define LEN (len - (*buf - start_buf))

uint32_t locate_start_func(module_t *m, lookup_cache_t *lookup, int argc, char **argv)
{
  uint32_t        start_func = UINT32_MAX;
  const uint8_t  *bufptr     = m->section[8].buf;
  const uint8_t **buf        = &bufptr;
  size_t          i          = 0;
  const char *    cstyle[]   = { "main", "__main_argc_argv" };
  if (m->section[8].len > 0) {
    leb_u32(buf, m->section[8].len, &start_func);
    return start_func;
  }
  /* C/C++ main style */
  for (i=0; i<(sizeof(cstyle)/sizeof(cstyle[0])); i++) {
    start_func = lookup_export(lookup, cstyle[i]);
    if (start_func < UINT32_MAX) {
      /* Push argv, argc on stack */
      return start_func;
    }
  }
}

const char *execute_wasm(const uint8_t *start_buf, size_t len, int argc, char **argv)
{
  struct module_st   module       = { 0 };
  const uint8_t     *bufptr       = start_buf;
  const uint8_t    **buf          = &bufptr;
  lookup_cache_t    *lookup       = NULL;
  size_t             lookup_size  = 0;
  uint32_t           start_func   = 0;

  if (!is_valid_wasm(start_buf, len, &module)) {
    return "Invalid WASM";
  }

  lookup_size = lookup_alloc(&module, NULL);
  lookup = malloc(lookup_size);
  lookup_alloc(&module, &lookup);

  start_func = locate_start_func(&module, lookup, argc, argv);
  printf("start_func: %u\n", start_func);

  return NULL;
}

/*
const char *execute_wast(const uint8_t *start_buf, size_t len, int argc, char **argv)
{
  struct module_st   module       = { 0 };
  const uint8_t     *bufptr       = start_buf;
  const uint8_t    **buf          = &bufptr;
  lookup_cache_t    *lookup       = NULL;
  size_t             lookup_size  = 0;
  uint32_t           start_func   = 0;

  if (!is_valid_wast(start_buf, len, &module)) {
    return "Invalid WASM";
  }

  lookup_size = lookup_alloc(&module, NULL);
  lookup = malloc(lookup_size);
  lookup_alloc(&module, &lookup);

  start_func = locate_start_func(&module, lookup, argc, argv);
  printf("start_func: %u\n", start_func);

  return NULL;
}
*/

