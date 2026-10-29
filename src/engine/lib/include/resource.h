#ifndef WASTE_RESOURCE_H
#define WASTE_RESOURCE_H

#include <stdint.h>

/* Borrowed session policy. All retained pages/files release their charge before
 * its owner is destroyed. NULL preserves the ordinary runtime's existing limits.
 * Counters belong to backing objects, never pathname/descriptor/clone aliases. */
typedef enum {
    WASTE_RESOURCE_NONE = 0,
    WASTE_RESOURCE_MEMORY,
    WASTE_RESOURCE_FILESYSTEM,
    WASTE_RESOURCE_PROCESSES,
    WASTE_RESOURCE_INSTRUCTIONS
} waste_resource_kind;

typedef struct waste_resource_budget {
    uint64_t memory_pages, memory_peak, memory_limit;
    uint64_t file_bytes, file_peak, file_limit;
    uint64_t instructions, instruction_limit;
    uint32_t process_limit, process_peak;
    waste_resource_kind exhausted;
} waste_resource_budget;

static inline int waste_resource_reserve(waste_resource_budget *budget,
    waste_resource_kind kind, uint64_t amount) {
    if (!budget || !amount) return 1;
    uint64_t *used = kind == WASTE_RESOURCE_MEMORY ? &budget->memory_pages : &budget->file_bytes;
    uint64_t *peak = kind == WASTE_RESOURCE_MEMORY ? &budget->memory_peak : &budget->file_peak;
    uint64_t limit = kind == WASTE_RESOURCE_MEMORY ? budget->memory_limit : budget->file_limit;
    if (*used > limit || amount > limit - *used) {
        if (!budget->exhausted) budget->exhausted = kind;
        return 0;
    }
    *used += amount;
    if (*used > *peak) *peak = *used;
    return 1;
}

static inline void waste_resource_release(waste_resource_budget *budget,
    waste_resource_kind kind, uint64_t amount) {
    if (!budget) return;
    uint64_t *used = kind == WASTE_RESOURCE_MEMORY ? &budget->memory_pages : &budget->file_bytes;
    /* Saturation keeps teardown safe even after a failed partial construction. */
    *used = amount > *used ? 0 : *used - amount;
}

#endif
