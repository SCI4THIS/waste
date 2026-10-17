/* Native sanitizer adapter for the browser runtime's private allocator. */
#include <stddef.h>
size_t browser_test_memory_size(unsigned);
size_t browser_test_memory_grow(unsigned, size_t);
#define __builtin_wasm_memory_size browser_test_memory_size
#define __builtin_wasm_memory_grow browser_test_memory_grow
#define __heap_base browser_test_heap
#define malloc browser_test_malloc
#define calloc browser_test_calloc
#define realloc browser_test_realloc
#define free browser_test_free
#define WASTE_ALLOCATOR_ONLY 1
#define strtod browser_test_strtod
#define strtof browser_test_strtof
#define getenv browser_test_getenv
