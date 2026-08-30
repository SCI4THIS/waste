#ifndef FILEGUARD_LOOKUP_H__
#define FILEGUARD_LOOKUP_H__

#include "validate.h"

        struct lookup_cache_st;
typedef struct lookup_cache_st lookup_cache_t;

size_t   lookup_alloc (module_t *m, lookup_cache_t **lookup);
/* Returns UINT32_MAX on error */
uint32_t lookup_export(lookup_cache_t *lookup, const char *name);

#endif
