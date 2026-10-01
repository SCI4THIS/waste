#ifndef WASTE_SEARCH_H
#define WASTE_SEARCH_H

/* Minimal search.h — provides the tsearch family for ncurses lib_tparm.c.
   The guest libc does not implement these; they are stubbed out so the
   library compiles and falls back to linear search internally. */

#include <stddef.h>

typedef enum { preorder, postorder, endorder, leaf } VISIT;

void *tsearch(const void *key, void **rootp,
              int (*compar)(const void *, const void *));
void *tfind(const void *key, void *const *rootp,
            int (*compar)(const void *, const void *));
void *tdelete(const void *key, void **rootp,
              int (*compar)(const void *, const void *));
void twalk(const void *root,
           void (*action)(const void *, VISIT, int));
void tdestroy(void *root, void (*free_node)(void *));

#endif
