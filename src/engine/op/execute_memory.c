#include "../runtime_internal.h"
#include "wasm/opcode.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static exec_memory_page *allocate_memory_page(void) {
    exec_memory_page *page = calloc(1, sizeof(*page));
    if (!page) return NULL;
    page->bytes = calloc(EXEC_PAGE_SIZE, 1);
    if (!page->bytes) {
        free(page);
        return NULL;
    }
    page->refs = 1;
    return page;
}

void exec_memory_page_release(exec_memory_page *page) {
    if (!page || !page->refs) return;
    page->refs--;
    if (!page->refs) {
        free(page->bytes);
        free(page);
    }
}

void exec_memory_page_retain(exec_memory_page *page) {
    if (page && page->refs != UINT32_MAX) page->refs++;
}

static exec_status detach_memory_page(exec_memory *memory, uint64_t page,
                                      exec_error *error) {
    exec_memory_page *old = memory->page_data[page];
    exec_memory_page *copy;
    if (!old || old->refs <= 1 || old->shared) return EXEC_OK;
    copy = allocate_memory_page();
    if (!copy) return exec_fail(error, EXEC_ERROR_TRAP,
                                "memory page allocation failed");
    memcpy(copy->bytes, old->bytes, EXEC_PAGE_SIZE);
    old->refs--;
    memory->page_data[page] = copy;
    return EXEC_OK;
}

/* ---- Stack and value helpers ---- */

int stack_push(exec_stack *s, wasm_value v) {
    if (s->top >= EXEC_MAX_STACK) return 0;
    s->vals[s->top++] = v;
    return 1;
}

int stack_pop(exec_stack *s, wasm_value *out) {
    if (s->top <= 0) return 0;
    *out = s->vals[--s->top];
    return 1;
}

/* ---- Arithmetic and memory helpers ---- */

static exec_status memory_fault(exec_error *error,
                                exec_memory_fault_kind kind,
                                uint64_t address, size_t length,
                                uint8_t access, const char *message) {
    if (error) {
        error->memory_fault = kind;
        error->memory_fault_address = address;
        error->memory_fault_length = length;
        error->memory_fault_access = access;
    }
    return exec_fail(error, EXEC_ERROR_TRAP, message);
}

int address_value(const wasm_value *value, int is_64, uint64_t *out) {
    if (value->type != (is_64 ? WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
        return 0;
    *out = is_64 ? (uint64_t)value->i64 : (uint64_t)(uint32_t)value->i32;
    return 1;
}

uint64_t load_le(const uint8_t *memory, size_t address, uint32_t width) {
    uint64_t value = 0;
    for (uint32_t i = 0; i < width; i++) value |= (uint64_t)memory[address + i] << (8u * i);
    return value;
}

void store_le(uint8_t *memory, size_t address, uint64_t value, uint32_t width) {
    for (uint32_t i = 0; i < width; i++) memory[address + i] = (uint8_t)(value >> (8u * i));
}

static exec_status memory_range(const exec_memory *memory, uint64_t offset,
                                size_t length, size_t *address,
                                exec_error *error) {
    uint64_t visible_pages = memory && memory->process_virtual_memory ?
        memory->pages : (memory ? memory->linear_pages : 0);
    uint64_t size = (visible_pages <= UINT64_MAX / EXEC_PAGE_SIZE) ?
        visible_pages * EXEC_PAGE_SIZE : 0;
    if (!memory || (memory->pages && (!memory->page_data ||
                                      !memory->page_protection)) ||
        offset > size ||
        (uint64_t)length > size - offset || offset > SIZE_MAX)
        return memory_fault(error, EXEC_MEMORY_FAULT_OUT_OF_RANGE, offset,
                            length, 0, "out of bounds memory access");
    if (address) *address = (size_t)offset;
    return EXEC_OK;
}

static exec_status memory_permissions(const exec_memory *memory,
                                      uint64_t offset, size_t length,
                                      uint8_t required, exec_error *error) {
    if (!length) return EXEC_OK;
    uint64_t first = offset / EXEC_PAGE_SIZE;
    uint64_t last = (offset + (uint64_t)length - 1) / EXEC_PAGE_SIZE;
    for (uint64_t page = first; page <= last; page++) {
        if ((memory->page_protection[page] & required) != required) {
            exec_memory_fault_kind kind = exec_memory_page_is_mapped(
                memory, page) ? EXEC_MEMORY_FAULT_PROTECTION :
                EXEC_MEMORY_FAULT_UNMAPPED;
            return memory_fault(error, kind, offset, length, required,
                                kind == EXEC_MEMORY_FAULT_UNMAPPED ?
                                "unmapped memory access" :
                                "memory protection violation");
        }
    }
    return EXEC_OK;
}

exec_status exec_memory_read(const exec_memory *memory, uint64_t offset,
                             void *destination, size_t length,
                             exec_error *error) {
    size_t address = 0;
    exec_status status = memory_range(memory, offset, length, &address, error);
    if (status != EXEC_OK) return status;
    if (memory->access_check) {
        status = (exec_status)memory->access_check(
            memory, offset, length, EXEC_MEMORY_PROT_READ,
            memory->access_check_context, error);
        if (status != EXEC_OK) return status;
    }
    status = memory_permissions(memory, offset, length, EXEC_MEMORY_PROT_READ,
                                error);
    if (status != EXEC_OK) return status;
    if (length && !destination)
        return exec_fail(error, EXEC_ERROR_FORMAT,
                         "null memory read destination");
    if (length) {
        uint8_t *out = (uint8_t *)destination;
        while (length) {
            uint64_t page = (uint64_t)address / EXEC_PAGE_SIZE;
            size_t in_page = address % EXEC_PAGE_SIZE;
            size_t chunk = EXEC_PAGE_SIZE - in_page;
            if (chunk > length) chunk = length;
            if (memory->page_data[page])
                memcpy(out, memory->page_data[page]->bytes + in_page, chunk);
            else
                memset(out, 0, chunk);
            out += chunk;
            address += chunk;
            length -= chunk;
        }
    }
    return EXEC_OK;
}

exec_status exec_memory_read_backing(const exec_memory *memory,
                                     uint64_t offset, void *destination,
                                     size_t length, exec_error *error) {
    size_t address = 0;
    exec_status status = memory_range(memory, offset, length, &address, error);
    if (status != EXEC_OK) return status;
    if (length && !destination)
        return exec_fail(error, EXEC_ERROR_FORMAT,
                         "null backing read destination");
    if (length) {
        uint8_t *out = (uint8_t *)destination;
        while (length) {
            uint64_t page = (uint64_t)address / EXEC_PAGE_SIZE;
            size_t in_page = address % EXEC_PAGE_SIZE;
            size_t chunk = EXEC_PAGE_SIZE - in_page;
            if (chunk > length) chunk = length;
            if (memory->page_data[page])
                memcpy(out, memory->page_data[page]->bytes + in_page, chunk);
            else
                memset(out, 0, chunk);
            out += chunk;
            address += chunk;
            length -= chunk;
        }
    }
    return EXEC_OK;
}

exec_status exec_memory_write(exec_memory *memory, uint64_t offset,
                              const void *source, size_t length,
                              exec_error *error) {
    size_t address = 0;
    exec_status status = memory_range(memory, offset, length, &address, error);
    if (status != EXEC_OK) return status;
    if (memory->access_check) {
        status = (exec_status)memory->access_check(
            memory, offset, length, EXEC_MEMORY_PROT_WRITE,
            memory->access_check_context, error);
        if (status != EXEC_OK) return status;
    }
    status = memory_permissions(memory, offset, length, EXEC_MEMORY_PROT_WRITE,
                                error);
    if (status != EXEC_OK) return status;
    if (length && !source)
        return exec_fail(error, EXEC_ERROR_FORMAT,
                         "null memory write source");
    if (length) {
        const uint8_t *in = (const uint8_t *)source;
        while (length) {
            uint64_t page = (uint64_t)address / EXEC_PAGE_SIZE;
            size_t in_page = address % EXEC_PAGE_SIZE;
            size_t chunk = EXEC_PAGE_SIZE - in_page;
            if (chunk > length) chunk = length;
            if (!memory->page_data[page]) {
                memory->page_data[page] = allocate_memory_page();
                if (!memory->page_data[page])
                    return exec_fail(error, EXEC_ERROR_TRAP,
                                     "memory page allocation failed");
            }
            if (detach_memory_page(memory, page, error) != EXEC_OK)
                return error->status;
            memcpy(memory->page_data[page]->bytes + in_page, in, chunk);
            memory->page_data[page]->dirty = 1;
            in += chunk;
            address += chunk;
            length -= chunk;
        }
    }
    return EXEC_OK;
}

exec_status exec_memory_copy(exec_memory *destination,
                             uint64_t destination_offset,
                             const exec_memory *source,
                             uint64_t source_offset, size_t length,
                             exec_error *error) {
    size_t destination_address = 0, source_address = 0;
    exec_status status = memory_range(destination, destination_offset, length,
                                      &destination_address, error);
    if (status != EXEC_OK) return status;
    status = memory_range(source, source_offset, length, &source_address, error);
    if (status != EXEC_OK) return status;
    if (!length) return EXEC_OK;
    if (destination == source && destination_offset > source_offset &&
        destination_offset - source_offset < length) {
        size_t remaining = length;
        uint8_t chunk[256];
        while (remaining) {
            size_t part = remaining > sizeof(chunk) ? sizeof(chunk) : remaining;
            remaining -= part;
            status = exec_memory_read(source, source_offset + remaining,
                                      chunk, part, error);
            if (status != EXEC_OK) return status;
            status = exec_memory_write(destination, destination_offset + remaining,
                                       chunk, part, error);
            if (status != EXEC_OK) return status;
        }
        return EXEC_OK;
    }
    {
        size_t done = 0;
        uint8_t chunk[256];
        while (done < length) {
            size_t part = length - done;
            if (part > sizeof(chunk)) part = sizeof(chunk);
            status = exec_memory_read(source, source_offset + done,
                                      chunk, part, error);
            if (status != EXEC_OK) return status;
            status = exec_memory_write(destination, destination_offset + done,
                                       chunk, part, error);
            if (status != EXEC_OK) return status;
            done += part;
        }
    }
    return EXEC_OK;
}

exec_status exec_memory_fill(exec_memory *memory, uint64_t offset,
                             uint8_t value, size_t length, exec_error *error) {
    size_t address = 0;
    exec_status status = memory_range(memory, offset, length, &address, error);
    if (status != EXEC_OK) return status;
    while (length) {
        uint64_t page = (uint64_t)address / EXEC_PAGE_SIZE;
        size_t in_page = address % EXEC_PAGE_SIZE;
        size_t chunk = EXEC_PAGE_SIZE - in_page;
        if (chunk > length) chunk = length;
        if (value == 0 && !memory->page_data[page]) {
            address += chunk;
            length -= chunk;
            continue;
        }
        if (!memory->page_data[page]) {
            memory->page_data[page] = allocate_memory_page();
            if (!memory->page_data[page])
                return exec_fail(error, EXEC_ERROR_TRAP,
                                 "memory page allocation failed");
        }
        if (detach_memory_page(memory, page, error) != EXEC_OK)
            return error->status;
        memset(memory->page_data[page]->bytes + in_page, value, chunk);
        memory->page_data[page]->dirty = 1;
        address += chunk;
        length -= chunk;
    }
    return EXEC_OK;
}

void exec_memory_release(exec_memory *memory) {
    if (!memory) return;
    if (memory->page_data)
        for (uint64_t i = 0; i < memory->pages; i++)
            exec_memory_page_release(memory->page_data[i]);
    free(memory->page_data);
    memory->page_data = NULL;
    free(memory->page_protection);
    memory->page_protection = NULL;
    free(memory->mappings);
    memory->mappings = NULL;
    memory->mapping_count = 0;
    memory->mapping_capacity = 0;
}

exec_status exec_memory_resize_pages(exec_memory *memory, uint64_t pages,
                                     exec_error *error) {
    exec_memory_page **grown;
    uint8_t *protection;
    exec_memory_mapping *mappings;
    if (!memory || pages < memory->linear_pages ||
        pages > SIZE_MAX / sizeof(*grown))
        return exec_fail(error, EXEC_ERROR_TRAP, "memory page map too large");
    if (pages == memory->linear_pages) return EXEC_OK;
    if (pages <= memory->pages) {
        if (exec_memory_map_pages(memory, memory->linear_pages,
                                  pages - memory->linear_pages,
                                  EXEC_MEMORY_PROT_READ |
                                  EXEC_MEMORY_PROT_WRITE, 0, error) != EXEC_OK)
            return error ? error->status : EXEC_ERROR_TRAP;
        memory->linear_pages = pages;
        return EXEC_OK;
    }
    grown = realloc(memory->page_data, (size_t)pages * sizeof(*grown));
    if (!grown) return exec_fail(error, EXEC_ERROR_TRAP,
                                 "memory page map allocation failed");
    protection = realloc(memory->page_protection, (size_t)pages);
    if (!protection) {
        memory->page_data = grown;
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "memory protection map allocation failed");
    }
    if (memory->mapping_count && memory->mappings[
            memory->mapping_count - 1].first_page +
            memory->mappings[memory->mapping_count - 1].page_count ==
            memory->pages &&
        memory->mappings[memory->mapping_count - 1].protection ==
            (EXEC_MEMORY_PROT_READ | EXEC_MEMORY_PROT_WRITE)) {
        memory->mappings[memory->mapping_count - 1].page_count +=
            pages - memory->pages;
    } else {
        if (memory->mapping_count == UINT32_MAX) {
            memory->page_data = grown;
            memory->page_protection = protection;
            return exec_fail(error, EXEC_ERROR_TRAP,
                             "memory mapping metadata limit exceeded");
        }
        mappings = realloc(memory->mappings,
                           (size_t)(memory->mapping_count + 1) *
                           sizeof(*mappings));
        if (!mappings) {
            memory->page_data = grown;
            memory->page_protection = protection;
            return exec_fail(error, EXEC_ERROR_TRAP,
                             "memory mapping metadata allocation failed");
        }
        mappings[memory->mapping_count] = (exec_memory_mapping){
            memory->pages, pages - memory->pages,
            EXEC_MEMORY_PROT_READ | EXEC_MEMORY_PROT_WRITE, 0};
        memory->mappings = mappings;
        memory->mapping_count++;
        memory->mapping_capacity = memory->mapping_count;
    }
    memset(grown + memory->pages, 0,
           (size_t)(pages - memory->pages) * sizeof(*grown));
    memset(protection + memory->pages, EXEC_MEMORY_PROT_READ |
           EXEC_MEMORY_PROT_WRITE, (size_t)(pages - memory->pages));
    memory->page_data = grown;
    memory->page_protection = protection;
    memory->pages = pages;
    memory->linear_pages = pages;
    return EXEC_OK;
}

exec_status exec_memory_reserve_virtual_pages(exec_memory *memory,
                                              uint64_t pages,
                                              exec_error *error) {
    exec_memory_page **grown;
    uint8_t *protection;
    uint64_t limit;
    if (!memory || pages < memory->pages || pages > SIZE_MAX / sizeof(*grown))
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "virtual page reservation is invalid");
    limit = memory->virtual_max_pages ? memory->virtual_max_pages :
        (memory->has_max ? memory->max_pages :
         (memory->is_64 ? EXEC_MEM64_MAX_PAGES : EXEC_MEM32_MAX_PAGES));
    if (pages > limit)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "virtual page reservation exceeds memory maximum");
    if (pages == memory->pages) return EXEC_OK;
    grown = realloc(memory->page_data, (size_t)pages * sizeof(*grown));
    if (!grown)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "virtual page table allocation failed");
    protection = realloc(memory->page_protection, (size_t)pages);
    if (!protection) {
        memory->page_data = grown;
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "virtual protection table allocation failed");
    }
    memset(grown + memory->pages, 0,
           (size_t)(pages - memory->pages) * sizeof(*grown));
    memset(protection + memory->pages, 0,
           (size_t)(pages - memory->pages));
    memory->page_data = grown;
    memory->page_protection = protection;
    memory->pages = pages;
    return EXEC_OK;
}

exec_status exec_memory_promote_linear_pages(exec_memory *memory,
                                             uint64_t pages,
                                             exec_error *error) {
    if (!memory || pages < memory->linear_pages || pages > memory->pages)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "linear memory promotion is invalid");
    memory->linear_pages = pages;
    return EXEC_OK;
}

static exec_status build_shared_mapping(const exec_memory *memory,
                                        uint64_t first_page,
                                        uint64_t page_count,
                                        exec_memory_mapping **replacement_out,
                                        uint32_t *count_out,
                                        exec_error *error) {
    exec_memory_mapping *replacement;
    uint64_t end_page;
    uint32_t replacement_count = 0;
    *replacement_out = NULL;
    *count_out = 0;
    if (!page_count || !memory->mapping_count) return EXEC_OK;
    end_page = first_page + page_count;
    if (memory->mapping_count > (UINT32_MAX - 1) / 3)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "memory mapping metadata limit exceeded");
    replacement = calloc((size_t)memory->mapping_count * 3 + 1,
                         sizeof(*replacement));
    if (!replacement)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "memory mapping metadata allocation failed");
    for (uint32_t i = 0; i < memory->mapping_count; i++) {
        exec_memory_mapping old = memory->mappings[i];
        uint64_t old_end = old.first_page + old.page_count;
        uint64_t overlap_start = old.first_page > first_page ?
            old.first_page : first_page;
        uint64_t overlap_end = old_end < end_page ? old_end : end_page;
        if (overlap_start >= overlap_end) {
            replacement[replacement_count++] = old;
            continue;
        }
        if (old.first_page < overlap_start)
            replacement[replacement_count++] = (exec_memory_mapping){
                old.first_page, overlap_start - old.first_page,
                old.protection, old.flags};
        replacement[replacement_count++] = (exec_memory_mapping){
            overlap_start, overlap_end - overlap_start, old.protection,
            (uint8_t)(old.flags | EXEC_MEMORY_MAPPING_SHARED)};
        if (overlap_end < old_end)
            replacement[replacement_count++] = (exec_memory_mapping){
                overlap_end, old_end - overlap_end, old.protection,
                old.flags};
    }
    *replacement_out = replacement;
    *count_out = replacement_count;
    return EXEC_OK;
}

exec_status exec_memory_share_pages(exec_memory *destination,
                                    uint64_t destination_first,
                                    exec_memory *source, uint64_t source_first,
                                    uint64_t page_count, exec_error *error) {
    exec_memory_page **pages;
    exec_memory_mapping *destination_mappings = NULL;
    exec_memory_mapping *source_mappings = NULL;
    uint32_t destination_mapping_count = 0;
    uint32_t source_mapping_count = 0;
    if (!destination || !source ||
        (destination->pages && !destination->page_data) ||
        (source->pages && !source->page_data) ||
        destination_first > destination->pages ||
        source_first > source->pages ||
        page_count > destination->pages - destination_first ||
        page_count > source->pages - source_first ||
        page_count > SIZE_MAX / sizeof(*pages))
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "shared memory range invalid");
    if (!page_count) return EXEC_OK;
    pages = calloc((size_t)page_count, sizeof(*pages));
    if (!pages)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "shared memory page map allocation failed");
    if (build_shared_mapping(destination, destination_first, page_count,
                             &destination_mappings,
                             &destination_mapping_count, error) != EXEC_OK)
        goto mapping_fail;
    if (source != destination &&
        build_shared_mapping(source, source_first, page_count,
                             &source_mappings, &source_mapping_count,
                             error) != EXEC_OK)
        goto mapping_fail;
    for (uint64_t i = 0; i < page_count; i++) {
        pages[i] = source->page_data[source_first + i];
        if (pages[i]) exec_memory_page_retain(pages[i]);
    }
    free(destination->mappings);
    destination->mappings = destination_mappings;
    destination->mapping_count = destination_mapping_count;
    destination->mapping_capacity = destination_mapping_count;
    destination_mappings = NULL;
    if (source != destination) {
        free(source->mappings);
        source->mappings = source_mappings;
        source->mapping_count = source_mapping_count;
        source->mapping_capacity = source_mapping_count;
        source_mappings = NULL;
    }
    for (uint64_t i = 0; i < page_count; i++)
        if (pages[i]) pages[i]->shared = 1;
    for (uint64_t i = 0; i < page_count; i++) {
        exec_memory_page_release(destination->page_data[destination_first + i]);
        destination->page_data[destination_first + i] = pages[i];
    }
    free(pages);
    return EXEC_OK;
mapping_fail:
    free(destination_mappings);
    free(source_mappings);
    free(pages);
    return error ? error->status : EXEC_ERROR_TRAP;
}

exec_status exec_memory_bind_shared_page(exec_memory *memory,
                                         uint64_t page_index,
                                         exec_memory_page *page,
                                         exec_error *error) {
    if (!memory || page_index >= memory->pages || !memory->page_data || !page)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "shared memory page binding invalid");
    exec_memory_page_retain(page);
    page->shared = 1;
    exec_memory_page_release(memory->page_data[page_index]);
    memory->page_data[page_index] = page;
    return EXEC_OK;
}

exec_status exec_memory_mark_shared_pages(exec_memory *memory,
                                          uint64_t first_page,
                                          uint64_t page_count,
                                          exec_error *error) {
    if (!memory || first_page > memory->pages ||
        page_count > memory->pages - first_page)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "shared memory range invalid");
    for (uint64_t page = first_page; page < first_page + page_count; page++)
        if (memory->page_data[page]) memory->page_data[page]->shared = 1;
    return EXEC_OK;
}

int exec_memory_page_is_dirty(const exec_memory *memory, uint64_t page) {
    return memory && page < memory->pages && memory->page_data &&
           memory->page_data[page] && memory->page_data[page]->dirty;
}

exec_status exec_memory_clear_dirty_pages(exec_memory *memory,
                                          uint64_t first_page,
                                          uint64_t page_count,
                                          exec_error *error) {
    if (!memory || first_page > memory->pages ||
        page_count > memory->pages - first_page)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "dirty memory range invalid");
    for (uint64_t page = first_page; page < first_page + page_count; page++)
        if (memory->page_data[page]) memory->page_data[page]->dirty = 0;
    return EXEC_OK;
}

exec_status exec_memory_unmap_pages(exec_memory *memory, uint64_t first_page,
                                    uint64_t page_count, exec_error *error) {
    exec_memory_mapping *replacement;
    uint64_t end_page;
    uint32_t replacement_count = 0;
    if (!memory || (memory->pages &&
                    (!memory->page_data || !memory->page_protection)) ||
        first_page > memory->pages ||
        page_count > memory->pages - first_page)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "memory unmap range invalid");
    if (!page_count) return EXEC_OK;
    end_page = first_page + page_count;
    if (memory->mapping_count > (UINT32_MAX - 1) / 2)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "memory mapping metadata limit exceeded");
    replacement = calloc((size_t)memory->mapping_count * 2 + 1,
                         sizeof(*replacement));
    if (!replacement)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "memory mapping metadata allocation failed");
    for (uint32_t i = 0; i < memory->mapping_count; i++) {
        exec_memory_mapping old = memory->mappings[i];
        uint64_t old_end = old.first_page + old.page_count;
        uint64_t overlap_start = old.first_page > first_page ?
            old.first_page : first_page;
        uint64_t overlap_end = old_end < end_page ? old_end : end_page;
        if (overlap_start >= overlap_end) {
            replacement[replacement_count++] = old;
            continue;
        }
        if (old.first_page < overlap_start)
            replacement[replacement_count++] = (exec_memory_mapping){
                old.first_page, overlap_start - old.first_page,
                old.protection, old.flags};
        if (overlap_end < old_end)
            replacement[replacement_count++] = (exec_memory_mapping){
                overlap_end, old_end - overlap_end, old.protection,
                old.flags};
    }
    for (uint64_t page = first_page; page < end_page; page++) {
        exec_memory_page_release(memory->page_data[page]);
        memory->page_data[page] = NULL;
    }
    memset(memory->page_protection + first_page, 0, (size_t)page_count);
    free(memory->mappings);
    memory->mappings = replacement;
    memory->mapping_count = replacement_count;
    memory->mapping_capacity = replacement_count;
    return EXEC_OK;
}

exec_status exec_memory_map_pages(exec_memory *memory, uint64_t first_page,
                                  uint64_t page_count, uint8_t protection,
                                  uint8_t flags, exec_error *error) {
    exec_memory_mapping *replacement;
    uint64_t end_page;
    uint32_t replacement_count = 0;
    uint32_t i;
    if (!memory || (memory->pages &&
                    (!memory->page_data || !memory->page_protection)) ||
        (protection & ~(EXEC_MEMORY_PROT_READ |
                        EXEC_MEMORY_PROT_WRITE |
                        EXEC_MEMORY_PROT_EXEC)) ||
        (flags & ~(EXEC_MEMORY_MAPPING_SHARED |
                   EXEC_MEMORY_MAPPING_FIXED_NOREPLACE)) ||
        first_page > memory->pages ||
        page_count > memory->pages - first_page)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "memory map range invalid");
    if (!page_count) return EXEC_OK;
    end_page = first_page + page_count;
    for (uint64_t page = first_page; page < end_page; page++)
        if (exec_memory_page_is_mapped(memory, page))
            return exec_fail(error, EXEC_ERROR_TRAP,
                             "memory map range is already mapped");
    if (memory->mapping_count == UINT32_MAX)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "memory mapping metadata limit exceeded");
    replacement = calloc((size_t)memory->mapping_count + 1,
                         sizeof(*replacement));
    if (!replacement)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "memory mapping metadata allocation failed");
    for (i = 0; i < memory->mapping_count; i++) {
        if (replacement_count == i &&
            memory->mappings[i].first_page > first_page)
            replacement[replacement_count++] = (exec_memory_mapping){
                first_page, page_count, protection, flags};
        replacement[replacement_count++] = memory->mappings[i];
    }
    if (replacement_count == memory->mapping_count)
        replacement[replacement_count++] = (exec_memory_mapping){
            first_page, page_count, protection, flags};
    memset(memory->page_protection + first_page, protection,
           (size_t)page_count);
    free(memory->mappings);
    memory->mappings = replacement;
    memory->mapping_count = replacement_count;
    memory->mapping_capacity = replacement_count;
    return EXEC_OK;
}

int exec_memory_page_is_mapped(const exec_memory *memory, uint64_t page) {
    if (!memory || page >= memory->pages) return 0;
    for (uint32_t i = 0; i < memory->mapping_count; i++) {
        const exec_memory_mapping *mapping = &memory->mappings[i];
        if (page < mapping->first_page) break;
        if (page - mapping->first_page < mapping->page_count) return 1;
    }
    return 0;
}

exec_status exec_memory_set_protection(exec_memory *memory, uint64_t first_page,
                                       uint64_t page_count, uint8_t protection,
                                       exec_error *error) {
    exec_memory_mapping *replacement;
    uint64_t end_page;
    uint32_t replacement_count = 0;
    if (!memory || (memory->pages && !memory->page_protection) ||
        (protection & ~(EXEC_MEMORY_PROT_READ |
                                   EXEC_MEMORY_PROT_WRITE |
                                   EXEC_MEMORY_PROT_EXEC)) ||
        first_page > memory->pages || page_count > memory->pages - first_page)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "memory protection range invalid");
    if (!page_count) return EXEC_OK;
    end_page = first_page + page_count;
    if (memory->mapping_count > (UINT32_MAX - 1) / 3)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "memory mapping metadata limit exceeded");
    replacement = calloc((size_t)memory->mapping_count * 3 + 1,
                         sizeof(*replacement));
    if (!replacement)
        return exec_fail(error, EXEC_ERROR_TRAP,
                         "memory mapping metadata allocation failed");
    for (uint32_t i = 0; i < memory->mapping_count; i++) {
        exec_memory_mapping old = memory->mappings[i];
        uint64_t old_end = old.first_page + old.page_count;
        uint64_t overlap_start = old.first_page > first_page ?
            old.first_page : first_page;
        uint64_t overlap_end = old_end < end_page ? old_end : end_page;
        if (overlap_start >= overlap_end) {
            replacement[replacement_count++] = old;
            continue;
        }
        if (old.first_page < overlap_start) {
            replacement[replacement_count++] = (exec_memory_mapping){
                old.first_page, overlap_start - old.first_page,
                old.protection, old.flags};
        }
        replacement[replacement_count++] = (exec_memory_mapping){
            overlap_start, overlap_end - overlap_start, protection,
            old.flags};
        if (overlap_end < old_end) {
            replacement[replacement_count++] = (exec_memory_mapping){
                overlap_end, old_end - overlap_end, old.protection,
                old.flags};
        }
    }
    if (replacement_count > memory->mapping_capacity) {
        /* The replacement is already independently allocated, so committing
         * it cannot fail after page permissions are changed. */
        memory->mapping_capacity = replacement_count;
    }
    if (page_count)
        memset(memory->page_protection + first_page, protection,
               (size_t)page_count);
    free(memory->mappings);
    memory->mappings = replacement;
    memory->mapping_count = replacement_count;
    return EXEC_OK;
}

exec_status memory_address(exec_memory *memory, uint64_t base, uint64_t offset,
                           uint32_t width, size_t *address,
                           exec_error *err) {
    uint64_t visible_pages = memory && memory->process_virtual_memory ?
        memory->pages : (memory ? memory->linear_pages : 0);
    uint64_t size = (visible_pages <= UINT64_MAX / EXEC_PAGE_SIZE) ?
        visible_pages * EXEC_PAGE_SIZE : 0;
    if (!memory || (memory->pages && (!memory->page_data ||
                                      !memory->page_protection)) ||
        base > UINT64_MAX - offset)
        return memory_fault(err, EXEC_MEMORY_FAULT_OUT_OF_RANGE, base,
                            width, 0, "out of bounds memory access");
    uint64_t effective = base + offset;
    if (effective > size || width > size - effective || effective > SIZE_MAX)
        return memory_fault(err, EXEC_MEMORY_FAULT_OUT_OF_RANGE, effective,
                            width, 0, "out of bounds memory access");
    *address = (size_t)effective;
    return EXEC_OK;
}

exec_status exec_memory_instruction(waste_exec_context *context,
                                    const exec_instr *instr,
                                    exec_error *err) {
    waste_exec_engine *eng = context->engine;
    exec_stack *stack = context->operand_stack;

    if (instr->opcode >= 0x28 && instr->opcode <= 0x35) {
        wasm_value base, value;
        size_t address = 0;
        exec_memory *memory = eng->memories[instr->memory_index];
        if (!stack_pop(stack, &base))
            return exec_fail(err, EXEC_ERROR_TRAP, "load address missing");
        const wasm_opcode_info *op_info =
            wasm_opcode_get_info(instr->opcode);
        uint32_t width = op_info->load_width;
        value.type = (wasm_valtype)op_info->result_type;
        int sign = op_info->sign_extend;
        uint64_t base_address = memory->is_64 ? (uint64_t)base.i64 :
                                               (uint64_t)(uint32_t)base.i32;
        exec_status status = memory_address(memory, base_address,
                                            instr->u64_imm, width,
                                            &address, err);
        if (status != EXEC_OK) return status;
        uint8_t bytes[8];
        if (exec_memory_read(memory, address, bytes, width, err) != EXEC_OK)
            return err->status;
        uint64_t bits = load_le(bytes, 0, width);
        if (sign && width < 8 &&
            (bits & ((uint64_t)1 << (width * 8u - 1u))))
            bits |= UINT64_MAX << (width * 8u);
        memset(value.nan_mode, 0, sizeof(value.nan_mode));
        if (value.type == WASM_VALTYPE_I32)
            value.i32 = (int32_t)bits;
        else if (value.type == WASM_VALTYPE_I64)
            value.i64 = (int64_t)bits;
        else if (value.type == WASM_VALTYPE_F32) {
            uint32_t word = (uint32_t)bits;
            memcpy(&value.f32, &word, sizeof(word));
        } else {
            memcpy(&value.f64, &bits, sizeof(bits));
        }
        if (!stack_push(stack, value))
            return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
        return EXEC_OK;
    }

    if (instr->opcode >= 0x36 && instr->opcode <= 0x3e) {
        wasm_value value, base;
        size_t address = 0;
        uint64_t bits;
        exec_memory *memory = eng->memories[instr->memory_index];
        if (!stack_pop(stack, &value) || !stack_pop(stack, &base))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "store operands missing");
        const wasm_opcode_info *st_info =
            wasm_opcode_get_info(instr->opcode);
        uint32_t width = st_info->load_width;
        if (st_info->operand_type == WASM_VALTYPE_F32) {
            uint32_t word;
            memcpy(&word, &value.f32, sizeof(word));
            bits = word;
        } else if (st_info->operand_type == WASM_VALTYPE_F64) {
            memcpy(&bits, &value.f64, sizeof(bits));
        } else if (st_info->operand_type == WASM_VALTYPE_I64) {
            bits = (uint64_t)value.i64;
        } else {
            bits = (uint32_t)value.i32;
        }
        uint64_t base_address = memory->is_64 ? (uint64_t)base.i64 :
                                               (uint64_t)(uint32_t)base.i32;
        exec_status status = memory_address(memory, base_address,
                                            instr->u64_imm, width,
                                            &address, err);
        if (status != EXEC_OK) return status;
        uint8_t bytes[8];
        store_le(bytes, 0, bits, width);
        if (exec_memory_write(memory, address, bytes, width, err) != EXEC_OK)
            return err->status;
        return EXEC_OK;
    }

    exec_memory *memory = instr->memory_index < eng->memory_count ?
                          eng->memories[instr->memory_index] : NULL;
    if (!memory)
        return exec_fail(err, EXEC_ERROR_TRAP, "memory missing");
    if (instr->opcode == 0x3f) {
        wasm_value size = memory->is_64 ? i64_value(memory->linear_pages) :
                                         i32_value((uint32_t)memory->linear_pages);
        if (!stack_push(stack, size))
            return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
        return EXEC_OK;
    }

    wasm_value delta;
    if (!stack_pop(stack, &delta) ||
        delta.type != (memory->is_64 ? WASM_VALTYPE_I64 : WASM_VALTYPE_I32))
        return exec_fail(err, EXEC_ERROR_TRAP,
                         "memory.grow operand missing");
    uint64_t old_pages = memory->linear_pages;
    uint64_t add = memory->is_64 ? (uint64_t)delta.i64 :
                                   (uint64_t)(uint32_t)delta.i32;
    uint64_t limit = memory->is_64 ? EXEC_MEM64_MAX_PAGES :
                                     EXEC_MEM32_MAX_PAGES;
    int failed = old_pages > UINT64_MAX - add;
    uint64_t pages = failed ? 0 : old_pages + add;
    if (failed || pages > limit ||
        (memory->has_max && pages > memory->max_pages) ||
        pages > SIZE_MAX / EXEC_PAGE_SIZE) {
        wasm_value failure = memory->is_64 ? i64_value(UINT64_MAX) :
                                             i32_value(UINT32_MAX);
        if (!stack_push(stack, failure))
            return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
        return EXEC_OK;
    }
    exec_error resize_error = {0};
    if (exec_memory_resize_pages(memory, pages, &resize_error) != EXEC_OK) {
        wasm_value failure = memory->is_64 ? i64_value(UINT64_MAX) :
                                             i32_value(UINT32_MAX);
        if (!stack_push(stack, failure))
            return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
        return EXEC_OK;
    }
    wasm_value old = memory->is_64 ? i64_value(old_pages) :
                                     i32_value((uint32_t)old_pages);
    if (!stack_push(stack, old))
        return exec_fail(err, EXEC_ERROR_TRAP, "stack overflow");
    return EXEC_OK;
}

exec_status exec_memory_bulk(waste_exec_context *context,
                             const exec_instr *instr, uint32_t subopcode,
                             exec_error *err) {
    waste_exec_engine *eng = context->engine;
    exec_stack *stack = context->operand_stack;

    if (subopcode == 10) { /* memory.copy */
        wasm_value n_v, src_v, dst_v;
        if (!stack_pop(stack, &n_v) || !stack_pop(stack, &src_v) ||
            !stack_pop(stack, &dst_v))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory.copy operands missing");
        if (instr->memory_index >= eng->memory_count ||
            instr->source_memory_index >= eng->memory_count)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory index out of range");
        exec_memory *dst_memory = eng->memories[instr->memory_index];
        exec_memory *src_memory = eng->memories[instr->source_memory_index];
        uint64_t dst, src, n;
        int length_is_64 = dst_memory->is_64 && src_memory->is_64;
        if (!address_value(&dst_v, dst_memory->is_64, &dst) ||
            !address_value(&src_v, src_memory->is_64, &src) ||
            !address_value(&n_v, length_is_64, &n))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory.copy operand type mismatch");
        uint64_t dst_pages = dst_memory->process_virtual_memory ?
            dst_memory->pages : dst_memory->linear_pages;
        uint64_t src_pages = src_memory->process_virtual_memory ?
            src_memory->pages : src_memory->linear_pages;
        uint64_t dst_size = dst_pages * EXEC_PAGE_SIZE;
        uint64_t src_size = src_pages * EXEC_PAGE_SIZE;
        if (dst > dst_size || n > dst_size - dst ||
            src > src_size || n > src_size - src || n > SIZE_MAX)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "out of bounds memory access");
        return exec_memory_copy(dst_memory, dst, src_memory, src,
                                (size_t)n, err);
    }

    if (subopcode == 11) { /* memory.fill */
        wasm_value n_v, value, dst_v;
        if (!stack_pop(stack, &n_v) || !stack_pop(stack, &value) ||
            !stack_pop(stack, &dst_v))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory.fill operands missing");
        if (instr->memory_index >= eng->memory_count)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory index out of range");
        exec_memory *memory = eng->memories[instr->memory_index];
        uint64_t dst, n;
        if (!address_value(&dst_v, memory->is_64, &dst) ||
            !address_value(&n_v, memory->is_64, &n) ||
            value.type != WASM_VALTYPE_I32)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory.fill operand type mismatch");
        uint64_t visible_pages = memory->process_virtual_memory ?
            memory->pages : memory->linear_pages;
        uint64_t size = visible_pages * EXEC_PAGE_SIZE;
        if (dst > size || n > size - dst || n > SIZE_MAX)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "out of bounds memory access");
        return exec_memory_fill(memory, dst, (uint8_t)value.i32,
                                (size_t)n, err);
    }

    if (subopcode == 8) { /* memory.init */
        wasm_value n_v, src_v, dst_v;
        if (!stack_pop(stack, &n_v) || !stack_pop(stack, &src_v) ||
            !stack_pop(stack, &dst_v))
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory.init operands missing");
        if (instr->memory_index >= eng->memory_count ||
            instr->u32_imm >= eng->data_count)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory.init index out of range");
        exec_memory *memory = eng->memories[instr->memory_index];
        uint64_t dst;
        uint32_t src = (uint32_t)src_v.i32;
        uint32_t n = (uint32_t)n_v.i32;
        if (!address_value(&dst_v, memory->is_64, &dst) ||
            src_v.type != WASM_VALTYPE_I32 || n_v.type != WASM_VALTYPE_I32)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "memory.init operand type mismatch");
        uint32_t data_length = eng->data_dropped[instr->u32_imm] ? 0 :
            eng->data_seg_lengths[instr->u32_imm];
        uint64_t visible_pages = memory->process_virtual_memory ?
            memory->pages : memory->linear_pages;
        uint64_t size = visible_pages * EXEC_PAGE_SIZE;
        if (dst > size || n > size - dst ||
            src > data_length || n > data_length - src)
            return exec_fail(err, EXEC_ERROR_TRAP,
                             "out of bounds memory access");
        return exec_memory_write(memory, dst,
                                 eng->data_segs[instr->u32_imm] + src,
                                 n, err);
    }

    if (instr->u32_imm >= eng->data_count)
        return exec_fail(err, EXEC_ERROR_TRAP,
                         "data segment index out of range");
    eng->data_dropped[instr->u32_imm] = 1;
    return EXEC_OK;
}
