/* Retained ownership checks for session budgets. Guest behavior stays in the
 * installed command fixtures; these assertions inspect backing-object identity. */
#include "runtime_internal.h"
#include "instantiate.h"
#include "lib/include/kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(condition) do { checks++; if (!(condition)) { \
    fprintf(stderr, "resource budget:%d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

static void pages(void) {
    waste_resource_budget budget = {.memory_limit = 2};
    exec_memory memory = {0}; exec_error error = {0};
    CHECK(wasm_instance_allocate_memory(&memory, 2, 2, 1, &error) == EXEC_OK);
    memory.resources = &budget;
    uint8_t byte = 1;
    CHECK(exec_memory_write(&memory, 0, &byte, 1, &error) == EXEC_OK);
    CHECK(budget.memory_pages == 1 && budget.memory_peak == 1);
    exec_memory_page *original = memory.page_data[0];
    exec_memory_page_retain(original);
    CHECK(budget.memory_pages == 1);
    byte = 2;
    CHECK(exec_memory_write(&memory, 0, &byte, 1, &error) == EXEC_OK);
    CHECK(memory.page_data[0] != original && original->bytes[0] == 1);
    CHECK(budget.memory_pages == 2 && budget.memory_peak == 2);
    CHECK(exec_memory_write(&memory, EXEC_PAGE_SIZE, &byte, 1, &error) != EXEC_OK);
    CHECK(budget.exhausted == WASTE_RESOURCE_MEMORY && budget.memory_pages == 2);
    CHECK(memory.page_data[1] == NULL);
    exec_memory_page_release(original);
    CHECK(budget.memory_pages == 1);
    budget.exhausted = WASTE_RESOURCE_NONE;
    CHECK(exec_memory_write(&memory, EXEC_PAGE_SIZE, &byte, 1, &error) == EXEC_OK);
    exec_memory_release(&memory);
    CHECK(!budget.memory_pages);

    exec_memory first = {0}, alias = {0};
    budget.memory_limit = 1; budget.memory_peak = 0;
    CHECK(wasm_instance_allocate_memory(&first, 1, 1, 1, &error) == EXEC_OK);
    CHECK(wasm_instance_allocate_memory(&alias, 1, 1, 1, &error) == EXEC_OK);
    first.resources = alias.resources = &budget;
    CHECK(exec_memory_write(&first, 0, &byte, 1, &error) == EXEC_OK);
    CHECK(exec_memory_bind_shared_page(&alias, 0, first.page_data[0], &error) == EXEC_OK);
    CHECK(budget.memory_pages == 1);
    byte = 3;
    CHECK(exec_memory_write(&alias, 0, &byte, 1, &error) == EXEC_OK);
    CHECK(first.page_data[0]->bytes[0] == 3 && budget.memory_peak == 1);
    exec_memory_release(&first);
    CHECK(budget.memory_pages == 1);
    exec_memory_release(&alias);
    CHECK(!budget.memory_pages);
}

static void files(void) {
    waste_resource_budget budget = {.file_limit = 8};
    posix_kernel *kernel = posix_kernel_create(0);
    CHECK(kernel != NULL);
    kernel->resources = &budget;
    posix_path_metadata info = {POSIX_NODE_REGULAR, 0644, 0, 0, 4,
        posix_kernel_allocate_inode(kernel), 0, 0};
    CHECK(!posix_kernel_path_add_data(kernel, "/data", &info, (const uint8_t *)"data", 4));
    CHECK(budget.file_bytes == 4);
    CHECK(!posix_kernel_linkat(kernel, POSIX_AT_FDCWD, (const uint8_t *)"/data", 5,
        POSIX_AT_FDCWD, (const uint8_t *)"/alias", 6, 0));
    CHECK(budget.file_bytes == 4);
    posix_kernel *clone = posix_kernel_clone(kernel);
    CHECK(clone != NULL && budget.file_bytes == 4);
    int fd = posix_kernel_open(kernel, (const uint8_t *)"/data", 5, 2, 0);
    CHECK(fd >= 0);
    CHECK(posix_kernel_file_write_at(kernel, fd, 6, "x", 1) == 1);
    CHECK(budget.file_bytes == 7);
    uint8_t copied[8] = {0xff};
    CHECK(posix_kernel_file_read_at(kernel, fd, 0, copied, 7) == 7);
    CHECK(!memcmp(copied, "data\0\0x", 7));
    CHECK(!posix_kernel_ftruncate(kernel, fd, 5) && budget.file_bytes == 5);
    CHECK(!posix_kernel_ftruncate(kernel, fd, 8) && budget.file_bytes == 8);
    CHECK(posix_kernel_ftruncate(kernel, fd, 9) < 0 && budget.file_bytes == 8);
    CHECK(budget.exhausted == WASTE_RESOURCE_FILESYSTEM && budget.file_peak == 8);
    uint64_t size;
    CHECK(!posix_kernel_file_size(kernel, fd, &size) && size == 8);
    posix_kernel_destroy(clone);
    CHECK(budget.file_bytes == 8);
    CHECK(!posix_kernel_path_unlink(kernel, (const uint8_t *)"/data", 5, 0));
    CHECK(!posix_kernel_path_unlink(kernel, (const uint8_t *)"/alias", 6, 0));
    CHECK(budget.file_bytes == 8); /* The open descriptor retains the charge. */
    CHECK(!posix_kernel_close(kernel, fd) && !budget.file_bytes);
    posix_kernel_destroy(kernel);
    CHECK(!budget.file_bytes);
}

int main(void) {
    pages(); files();
    printf("embedding resource ownership: %u checks passed\n", checks);
    return 0;
}
