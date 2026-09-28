#include "posix_stubs.h"
#include "../engine/runtime_internal.h"
#include "lib/include/kernel.h"
#include "lib/include/select.h"
#include "lib/include/path.h"
#include "lib/include/sys/mman.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern void waste_browser_record_transition(const char *event);

/* Host imports for POSIX operations delegated to JavaScript */

__attribute__((import_module("waste_host"), import_name("posix_open")))
extern int32_t waste_host_posix_open(const char *path, size_t length,
                                     int32_t flags, int32_t mode);

__attribute__((import_module("waste_host"), import_name("posix_close")))
extern int32_t waste_host_posix_close(int32_t descriptor);

__attribute__((import_module("waste_host"), import_name("posix_read")))
extern int32_t waste_host_posix_read(int32_t descriptor, void *buffer,
                                     uint32_t count);

__attribute__((import_module("waste_host"), import_name("posix_write")))
extern int32_t waste_host_posix_write(int32_t descriptor, const void *buffer,
                                      uint32_t count);

/* ---- POSIX stub helpers ---- */

static exec_status native_posix_memory(const waste_exec_engine *caller,
                                       exec_memory **memory_out,
                                       exec_error *error) {
    if (caller->memory) {
        *memory_out = caller->memory;
        return EXEC_OK;
    }
    error->status = EXEC_ERROR_NOT_FOUND;
    snprintf(error->message, sizeof(error->message),
             "POSIX host call: caller has no memory");
    return error->status;
}

static int native_posix_read_guest(exec_memory *memory, uint32_t offset,
                                   void *destination, size_t length) {
    exec_error error = {0};
    return memory && exec_memory_read(memory, offset, destination, length,
                                      &error) == EXEC_OK;
}

static int native_posix_write_guest(exec_memory *memory, uint32_t offset,
                                    const void *source, size_t length) {
    exec_error error = {0};
    return memory && exec_memory_write(memory, offset, source, length,
                                       &error) == EXEC_OK;
}

static exec_status native_posix_result(int32_t value, wasm_value *results,
                                       int *result_count) {
    results[0].type = WASM_VALTYPE_I32;
    results[0].i32 = value;
    *result_count = 1;
    return EXEC_OK;
}

static void native_posix_set_errno(native_store *store,
                                   const waste_exec_engine *caller, int value);

static exec_status native_posix_mmap(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error,
                                     const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    native_process_capsule *capsule;
    uint64_t address;
    uint64_t mapped_address = 0;
    uint8_t flags;
    uint8_t requested_protection;
    uint64_t file_size = 0;
    uint64_t object_id = 0;
    posix_file_object *file_object = NULL;
    uint64_t requested_length;
    int file_writable = 0;
    int status;
    (void)error;
    if (!store || arg_count != 6 || args[1].i32 <= 0 ||
        args[0].i32 < 0 || args[5].i64 < 0 ||
        (args[5].i64 % EXEC_PAGE_SIZE) != 0 ||
        !!(args[3].i32 & MAP_SHARED) == !!(args[3].i32 & MAP_PRIVATE) ||
        args[3].i32 & ~(MAP_SHARED | MAP_PRIVATE | MAP_ANONYMOUS |
                        MAP_FIXED_NOREPLACE)) {
        native_posix_set_errno(store, caller, POSIX_EINVAL);
        return native_posix_result(-1, results, result_count);
    }
    if ((args[3].i32 & MAP_ANONYMOUS) != 0 && args[4].i32 != -1) {
        native_posix_set_errno(store, caller, POSIX_EINVAL);
        return native_posix_result(-1, results, result_count);
    }
    if ((args[3].i32 & MAP_ANONYMOUS) == 0 && args[4].i32 < 0) {
        native_posix_set_errno(store, caller, POSIX_EBADF);
        return native_posix_result(-1, results, result_count);
    }
    requested_length = (uint32_t)args[1].i32;
    if ((args[3].i32 & MAP_ANONYMOUS) == 0 &&
        (posix_kernel_file_size(store->kernel, args[4].i32, &file_size) != 0 ||
         posix_kernel_file_identity(store->kernel, args[4].i32,
                                    &object_id, &file_writable) != 0 ||
         (uint64_t)args[5].i64 > file_size ||
         requested_length > file_size - (uint64_t)args[5].i64)) {
        native_posix_set_errno(store, caller, POSIX_EINVAL);
        return native_posix_result(-1, results, result_count);
    }
    if ((args[3].i32 & MAP_ANONYMOUS) == 0 &&
        (args[3].i32 & MAP_SHARED) != 0 &&
        (args[2].i32 & PROT_WRITE) != 0 && !file_writable) {
        native_posix_set_errno(store, caller, POSIX_EACCES);
        return native_posix_result(-1, results, result_count);
    }
    address = (uint32_t)args[0].i32;
    flags = (args[3].i32 & MAP_SHARED) ? EXEC_MEMORY_MAPPING_SHARED : 0;
    if (args[3].i32 & MAP_FIXED_NOREPLACE)
        flags |= EXEC_MEMORY_MAPPING_FIXED_NOREPLACE;
    requested_protection = (uint8_t)args[2].i32;
    capsule = native_store_active_capsule(store);
    status = native_process_capsule_mmap_range(
        capsule, address, (uint32_t)args[1].i32,
        (uint8_t)(requested_protection | EXEC_MEMORY_PROT_WRITE),
        flags, &mapped_address);
    if (status != 0 || mapped_address > INT32_MAX) {
        native_posix_set_errno(store, caller,
                               status < 0 ? -status : POSIX_ENOMEM);
        return native_posix_result(-1, results, result_count);
    }
    if ((args[3].i32 & MAP_ANONYMOUS) == 0 &&
        posix_kernel_file_retain(store->kernel, args[4].i32,
                                  &file_object, &file_writable) != 0) {
        (void)native_process_capsule_munmap_range(
            capsule, mapped_address, requested_length);
        native_posix_set_errno(store, caller, POSIX_EBADF);
        return native_posix_result(-1, results, result_count);
    }
    if ((args[3].i32 & MAP_ANONYMOUS) == 0) {
        uint8_t page[EXEC_PAGE_SIZE];
        uint64_t pages = (requested_length +
                          EXEC_PAGE_SIZE - 1) / EXEC_PAGE_SIZE;
        exec_memory *memory = capsule && capsule->engine ?
            capsule->engine->memory : NULL;
        for (uint64_t page_index = 0; page_index < pages; page_index++) {
            uint64_t file_offset = (uint64_t)args[5].i64 +
                                   page_index * EXEC_PAGE_SIZE;
            size_t bytes_to_read = file_offset < file_size ?
                (size_t)((file_size - file_offset) > EXEC_PAGE_SIZE ?
                         EXEC_PAGE_SIZE : file_size - file_offset) : 0;
            memset(page, 0, sizeof(page));
            if ((args[3].i32 & MAP_SHARED) != 0) {
                exec_memory_page *shared_page = NULL;
                status = native_store_shared_file_page(
                    store, file_object, file_offset, &shared_page);
                if (status != 0 || !memory ||
                    exec_memory_bind_shared_page(
                        memory, mapped_address / EXEC_PAGE_SIZE + page_index,
                        shared_page, error) != EXEC_OK) {
                    (void)native_process_capsule_munmap_range(
                        capsule, mapped_address, pages * EXEC_PAGE_SIZE);
                    posix_kernel_file_release(file_object);
                    native_posix_set_errno(store, caller,
                                           status < 0 ? -status : POSIX_EINVAL);
                    return native_posix_result(-1, results, result_count);
                }
                continue;
            }
            status = bytes_to_read == 0 ? 0 : posix_kernel_file_read_at(
                store->kernel, args[4].i32, file_offset, page,
                bytes_to_read);
            if (status != (int)bytes_to_read || !memory ||
                exec_memory_write(memory,
                                  mapped_address + page_index * EXEC_PAGE_SIZE,
                                  page, sizeof(page), error) != EXEC_OK) {
                (void)native_process_capsule_munmap_range(
                    capsule, mapped_address,
                    pages * EXEC_PAGE_SIZE);
                posix_kernel_file_release(file_object);
                native_posix_set_errno(store, caller,
                                       status < 0 ? -status : POSIX_EINVAL);
                return native_posix_result(-1, results, result_count);
            }
        }
        if (exec_memory_set_protection(memory,
                mapped_address / EXEC_PAGE_SIZE, pages,
                requested_protection, error) != EXEC_OK) {
            (void)native_process_capsule_munmap_range(
                capsule, mapped_address, pages * EXEC_PAGE_SIZE);
            posix_kernel_file_release(file_object);
            native_posix_set_errno(store, caller, POSIX_EINVAL);
            return native_posix_result(-1, results, result_count);
        }
        if ((args[3].i32 & MAP_SHARED) != 0 &&
            exec_memory_mark_shared_pages(
                memory, mapped_address / EXEC_PAGE_SIZE, pages, error) != EXEC_OK) {
            (void)native_process_capsule_munmap_range(
                capsule, mapped_address, pages * EXEC_PAGE_SIZE);
            posix_kernel_file_release(file_object);
            native_posix_set_errno(store, caller, POSIX_EINVAL);
            return native_posix_result(-1, results, result_count);
        }
        if (exec_memory_clear_dirty_pages(
                memory, mapped_address / EXEC_PAGE_SIZE, pages, error) != EXEC_OK) {
            (void)native_process_capsule_munmap_range(
                capsule, mapped_address, pages * EXEC_PAGE_SIZE);
            native_posix_set_errno(store, caller, POSIX_EINVAL);
            return native_posix_result(-1, results, result_count);
        }
    } else if (requested_protection !=
               (EXEC_MEMORY_PROT_READ | EXEC_MEMORY_PROT_WRITE)) {
        if (exec_memory_set_protection(
                capsule->engine->memory, mapped_address / EXEC_PAGE_SIZE,
                ((uint64_t)(uint32_t)args[1].i32 + EXEC_PAGE_SIZE - 1) /
                    EXEC_PAGE_SIZE,
                requested_protection, error) != EXEC_OK) {
            (void)native_process_capsule_munmap_range(
                capsule, mapped_address, (uint32_t)args[1].i32);
            native_posix_set_errno(store, caller, POSIX_EINVAL);
            return native_posix_result(-1, results, result_count);
        }
    }
    if ((args[3].i32 & MAP_ANONYMOUS) == 0 &&
        (!file_object || native_process_capsule_record_file_mapping(
            capsule, mapped_address, requested_length, file_object,
            (uint64_t)args[5].i64,
            (args[3].i32 & MAP_SHARED) != 0, file_writable) != 0)) {
        (void)native_process_capsule_munmap_range(
            capsule, mapped_address, requested_length);
        posix_kernel_file_release(file_object);
        native_posix_set_errno(store, caller, POSIX_ENOMEM);
        return native_posix_result(-1, results, result_count);
    }
    return native_posix_result((int32_t)mapped_address, results, result_count);
}

static int native_posix_sync_file_range(native_store *store,
                                        native_process_capsule *capsule,
                                        uint64_t address, uint64_t length,
                                        int require_file_coverage,
                                        exec_error *error) {
    exec_memory *memory = capsule && capsule->engine ?
        capsule->engine->memory : NULL;
    uint64_t end;
    uint8_t page[EXEC_PAGE_SIZE];
    if (!store || !memory || !length || address > UINT64_MAX - length)
        return -POSIX_EFAULT;
    end = address + length;
    if (require_file_coverage) {
        uint64_t cursor = address;
        while (cursor < end) {
            native_process_file_mapping *covering = NULL;
            for (uint32_t i = 0; i < capsule->file_mapping_count; i++) {
                native_process_file_mapping *candidate =
                    &capsule->file_mappings[i];
                if (candidate->address <= cursor &&
                    cursor < candidate->address + candidate->length) {
                    covering = candidate;
                    break;
                }
            }
            if (!covering) return -POSIX_ENOMEM;
            cursor = covering->address + covering->length < end ?
                covering->address + covering->length : end;
        }
    }
    for (uint32_t i = 0; i < capsule->file_mapping_count; i++) {
        native_process_file_mapping *mapping = &capsule->file_mappings[i];
        uint64_t file_size = mapping->file_object ?
            (uint64_t)mapping->file_object->data_capacity : 0;
        uint64_t mapping_end = mapping->address + mapping->length;
        uint64_t cursor = mapping->address > address ?
            mapping->address : address;
        uint64_t overlap_end = mapping_end < end ? mapping_end : end;
        if (!mapping->shared || cursor >= overlap_end) continue;
        if (mapping->file_offset > file_size ||
            mapping->length > file_size - mapping->file_offset)
            return -POSIX_EIO;
        while (cursor < overlap_end) {
            uint64_t page_index = cursor / EXEC_PAGE_SIZE;
            size_t in_page = (size_t)(cursor % EXEC_PAGE_SIZE);
            size_t chunk = EXEC_PAGE_SIZE - in_page;
            int status;
            if ((uint64_t)chunk > overlap_end - cursor)
                chunk = (size_t)(overlap_end - cursor);
            if (!exec_memory_page_is_dirty(memory, page_index)) {
                cursor += chunk;
                continue;
            }
            if (exec_memory_read_backing(memory, cursor, page, chunk, error) !=
                EXEC_OK)
                return -POSIX_EFAULT;
            if (!mapping->writable) return -POSIX_EACCES;
            status = posix_kernel_file_write_object(
                store->kernel, mapping->file_object,
                mapping->file_offset + cursor - mapping->address,
                page, chunk);
            if (status != (int)chunk)
                return status < 0 ? status : -POSIX_EINVAL;
            if (in_page == 0 && chunk == EXEC_PAGE_SIZE &&
                exec_memory_clear_dirty_pages(
                    memory, page_index, 1, error) != EXEC_OK)
                return -POSIX_EFAULT;
            cursor += chunk;
        }
    }
    return 0;
}

static exec_status native_posix_munmap(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    native_process_capsule *capsule;
    uint64_t length;
    int status;
    if (!store || arg_count != 2 || args[0].i32 < 0 || args[1].i32 <= 0 ||
        ((uint32_t)args[0].i32 % EXEC_PAGE_SIZE) != 0) {
        native_posix_set_errno(store, caller, POSIX_EINVAL);
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    }
    length = ((uint64_t)(uint32_t)args[1].i32 + EXEC_PAGE_SIZE - 1) /
             EXEC_PAGE_SIZE * EXEC_PAGE_SIZE;
    capsule = native_store_active_capsule(store);
    status = native_posix_sync_file_range(
        store, capsule, (uint32_t)args[0].i32, length, 0, error);
    if (status < 0) {
        native_posix_set_errno(store, caller, -status);
        return native_posix_result(-1, results, result_count);
    }
    status = native_process_capsule_munmap_range(
        capsule, (uint32_t)args[0].i32, length);
    if (status < 0) native_posix_set_errno(store, caller, -status);
    return native_posix_result(status, results, result_count);
}

static exec_status native_posix_msync(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    native_process_capsule *capsule;
    int status;
    if (!store || arg_count != 3 || args[0].i32 < 0 || args[1].i32 <= 0 ||
        ((uint32_t)args[0].i32 % EXEC_PAGE_SIZE) != 0 ||
        args[2].i32 != MS_SYNC ||
        (uint64_t)(uint32_t)args[0].i32 >
            UINT64_MAX - (uint32_t)args[1].i32) {
        native_posix_set_errno(store, caller, POSIX_EINVAL);
        return native_posix_result(-1, results, result_count);
    }
    capsule = native_store_active_capsule(store);
    status = native_posix_sync_file_range(
        store, capsule, (uint32_t)args[0].i32,
        (uint32_t)args[1].i32, 1, error);
    if (status < 0) {
        native_posix_set_errno(store, caller, -status);
        return native_posix_result(-1, results, result_count);
    }
    return native_posix_result(0, results, result_count);
}

static exec_status native_posix_mprotect(void *data, const wasm_value *args,
                                         int arg_count, wasm_value *results,
                                         int *result_count, exec_error *error,
                                         const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    (void)error;
    if (!store || arg_count != 3 || args[0].i32 < 0 || args[1].i32 <= 0) {
        native_posix_set_errno(store, caller, POSIX_EINVAL);
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    }
    if ((args[2].i32 & PROT_WRITE) != 0) {
        native_process_capsule *capsule = native_store_active_capsule(store);
        uint64_t address = (uint32_t)args[0].i32;
        uint64_t end = address + (uint32_t)args[1].i32;
        for (uint32_t i = 0; capsule && i < capsule->file_mapping_count; i++) {
            native_process_file_mapping *mapping =
                &capsule->file_mappings[i];
            uint64_t mapping_end = mapping->address + mapping->length;
            if (mapping->shared && !mapping->writable &&
                mapping->address < end && address < mapping_end) {
                native_posix_set_errno(store, caller, POSIX_EACCES);
                return native_posix_result(-1, results, result_count);
            }
        }
    }
    int status = native_process_capsule_mprotect_range(
        native_store_active_capsule(store), (uint32_t)args[0].i32,
        (uint32_t)args[1].i32, (uint8_t)args[2].i32);
    if (status < 0) native_posix_set_errno(store, caller, -status);
    return native_posix_result(status, results, result_count);
}

static exec_status native_posix_getpid(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    (void)args; (void)caller; (void)error;
    if (arg_count != 0) return native_posix_result(-POSIX_EINVAL, results, result_count);
    return native_posix_result(native_store_getpid((native_store *)data),
                               results, result_count);
}

static exec_status native_posix_getppid(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count, exec_error *error,
                                        const waste_exec_engine *caller) {
    (void)args; (void)caller; (void)error;
    if (arg_count != 0) return native_posix_result(-POSIX_EINVAL, results, result_count);
    return native_posix_result(native_store_getppid((native_store *)data),
                               results, result_count);
}

/* Resolve the caller's guest errno slot through its normal libc export. This
 * keeps errno in the instance's linear memory and avoids a host-global slot. */
static void native_posix_set_errno(native_store *store,
                                   const waste_exec_engine *caller, int value) {
    exec_error ignored = {0};
    uint32_t index;
    wasm_value result;
    int result_count = 0;
    waste_exec_engine *errno_engine = (waste_exec_engine *)caller;
    native_linked_module *env = native_registered_module(store, "env");
    if (env) errno_engine = env->engine;
    if (exec_find_export(errno_engine, "__errno_location", &index, &ignored) != EXEC_OK)
        return;
    if (exec_invoke(errno_engine, index, NULL, 0,
                    &result, &result_count, &ignored) != EXEC_OK ||
        result_count != 1 || result.type != WASM_VALTYPE_I32)
        return;
    exec_memory *memory = caller->memory;
    if (memory) {
        uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8),
                            (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
        exec_memory_write(memory, (uint32_t)result.i32, bytes, sizeof(bytes),
                          &ignored);
    }
}

static int native_posix_copy_guest_string(exec_memory *memory, uint32_t offset,
                                          char **copy_out) {
    size_t length = 0;
    uint64_t size;
    exec_error error = {0};
    if (!memory || !copy_out) return POSIX_EFAULT;
    size = memory->pages * UINT64_C(65536);
    if ((uint64_t)offset >= size) return POSIX_EFAULT;
    while ((uint64_t)offset + length < size &&
           length < NATIVE_EXEC_PATH_MAX) {
        uint8_t byte;
        if (exec_memory_read(memory, (uint64_t)offset + length, &byte, 1,
                             &error) != EXEC_OK)
            return POSIX_EFAULT;
        if (!byte) break;
        length++;
    }
    if ((uint64_t)offset + length >= size) return POSIX_EFAULT;
    if (length == NATIVE_EXEC_PATH_MAX) return POSIX_E2BIG;
    *copy_out = (char *)malloc(length + 1);
    if (!*copy_out) return POSIX_ENOMEM;
    if (exec_memory_read(memory, offset, *copy_out, length, &error) != EXEC_OK) {
        free(*copy_out);
        *copy_out = NULL;
        return POSIX_EFAULT;
    }
    (*copy_out)[length] = '\0';
    return 0;
}

static int native_posix_guest_path(const waste_exec_engine *caller,
                                   uint32_t offset, char **path,
                                   size_t *length, int *error_number) {
    int status;
    if (!caller || !path || !length || !error_number) return 0;
    status = native_posix_copy_guest_string(caller->memory, offset, path);
    if (status != 0) {
        *error_number = status;
        return 0;
    }
    *length = strlen(*path);
    if (*length == 0 || *length >= POSIX_PATH_MAX) {
        free(*path);
        *path = NULL;
        *error_number = *length >= POSIX_PATH_MAX ? POSIX_EINVAL : POSIX_EFAULT;
        return 0;
    }
    return 1;
}

static int native_posix_copy_guest_vector(exec_memory *memory, uint32_t vector,
                                          char **items, uint32_t capacity,
                                          uint32_t *count_out,
                                          int allow_null) {
    uint64_t size;
    uint32_t count = 0;
    if (!count_out || !items) return POSIX_EFAULT;
    *count_out = 0;
    if (vector == 0 && allow_null) return 0;
    if (!memory) return POSIX_EFAULT;
    size = memory->pages * UINT64_C(65536);
    for (;;) {
        uint64_t slot = (uint64_t)vector + (uint64_t)count * 4u;
        uint32_t pointer;
        char *copy = NULL;
        int status;
        if (count >= capacity || slot + 4u > size) return POSIX_E2BIG;
        uint8_t pointer_bytes[4];
        exec_error error = {0};
        if (exec_memory_read(memory, slot, pointer_bytes,
                             sizeof(pointer_bytes), &error) != EXEC_OK)
            return POSIX_E2BIG;
        pointer = (uint32_t)pointer_bytes[0] |
                  ((uint32_t)pointer_bytes[1] << 8) |
                  ((uint32_t)pointer_bytes[2] << 16) |
                  ((uint32_t)pointer_bytes[3] << 24);
        if (pointer == 0) break;
        status = native_posix_copy_guest_string(memory, pointer, &copy);
        if (status != 0) return status;
        items[count++] = copy;
    }
    *count_out = count;
    return 0;
}

static void native_posix_metadata_stat(const posix_path_metadata *metadata,
                                       posix_guest_stat *stat) {
    uint32_t type_mode = metadata->kind == POSIX_NODE_REGULAR ? 0100000u :
                         metadata->kind == POSIX_NODE_DIRECTORY ? 0040000u :
                         metadata->kind == POSIX_NODE_SYMLINK ? 0120000u : 0;
    memset(stat, 0, sizeof(*stat));
    stat->st_ino = metadata->inode;
    stat->st_mode = metadata->mode | type_mode;
    stat->st_nlink = metadata->kind == POSIX_NODE_DIRECTORY ? 2 : 1;
    stat->st_uid = metadata->uid;
    stat->st_gid = metadata->gid;
    stat->st_size = metadata->size;
    stat->st_blksize = 4096;
    stat->st_blocks = (metadata->size + 511) / 512;
}

/* ---- POSIX stub implementations ---- */

static exec_status native_posix_open(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error,
                                     const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    char *path = NULL;
    uint32_t offset;
    if (arg_count != 3 || native_posix_memory(caller, &memory, error) != EXEC_OK)
        return error->status;
    offset = (uint32_t)args[0].i32;
    int path_status = native_posix_copy_guest_string(memory, offset, &path);
    if (path_status != 0) {
        error->status = EXEC_ERROR_TRAP;
        snprintf(error->message, sizeof(error->message),
                 "invalid POSIX open path (%d)", path_status);
        return error->status;
    }
    size_t length = strlen(path);
    int32_t result;
    if (store && store->kernel)
        result = posix_kernel_open(store->kernel, (uint8_t *)path, length,
                                   args[1].i32, args[2].i32);
    else
        result = waste_host_posix_open(path, length, args[1].i32, args[2].i32);
    if (result < 0) {
        native_posix_set_errno(store, caller, -result);
        result = -1;
    }
    free(path);
    return native_posix_result(result, results, result_count);
}

static exec_status native_posix_shm_open(void *data, const wasm_value *args,
                                         int arg_count, wasm_value *results,
                                         int *result_count, exec_error *error,
                                         const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    char *name = NULL;
    size_t length = 0;
    int errno_value = 0;
    int result;
    (void)error;
    if (arg_count != 3 || !native_posix_guest_path(
            caller, (uint32_t)args[0].i32, &name, &length, &errno_value)) {
        native_posix_set_errno(store, caller,
                               errno_value ? errno_value : POSIX_EINVAL);
        return native_posix_result(-1, results, result_count);
    }
    result = store && store->kernel ? posix_kernel_shm_open(
        store->kernel, (const uint8_t *)name, length, args[1].i32,
        args[2].i32) : -POSIX_ENOSYS;
    if (result < 0) native_posix_set_errno(store, caller, -result);
    free(name);
    return native_posix_result(result, results, result_count);
}

static exec_status native_posix_shm_unlink(void *data, const wasm_value *args,
                                           int arg_count, wasm_value *results,
                                           int *result_count, exec_error *error,
                                           const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    char *name = NULL;
    size_t length = 0;
    int errno_value = 0;
    int result;
    (void)error;
    if (arg_count != 1 || !native_posix_guest_path(
            caller, (uint32_t)args[0].i32, &name, &length, &errno_value)) {
        native_posix_set_errno(store, caller,
                               errno_value ? errno_value : POSIX_EINVAL);
        return native_posix_result(-1, results, result_count);
    }
    result = store && store->kernel ? posix_kernel_shm_unlink(
        store->kernel, (const uint8_t *)name, length) : -POSIX_ENOSYS;
    if (result < 0) native_posix_set_errno(store, caller, -result);
    free(name);
    return native_posix_result(result < 0 ? -1 : 0, results, result_count);
}

static exec_status native_posix_close(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    int result;
    if (arg_count != 1) {
        error->status = EXEC_ERROR_FORMAT;
        snprintf(error->message, sizeof(error->message),
                 "POSIX close argument count mismatch");
        return error->status;
    }
    result = store && store->kernel ?
        posix_kernel_close(store->kernel, args[0].i32) :
        waste_host_posix_close(args[0].i32);
    if (result < 0) {
        native_posix_set_errno(store, caller, -result);
        result = -1;
    }
    return native_posix_result(result, results, result_count);
}

static exec_status native_posix_dup(void *data, const wasm_value *args,
                                    int arg_count, wasm_value *results,
                                    int *result_count, exec_error *error,
                                    const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    int result;
    (void)error;
    if (!store || !store->kernel || arg_count != 1) {
        native_posix_set_errno(store, caller, POSIX_EINVAL);
        return native_posix_result(-1, results, result_count);
    }
    result = posix_kernel_dup(store->kernel, args[0].i32);
    if (result < 0) {
        native_posix_set_errno(store, caller, -result);
        result = -1;
    }
    return native_posix_result(result, results, result_count);
}

static int native_posix_pipe_create(native_store *store,
                                    const waste_exec_engine *caller,
                                    uint32_t descriptor_pointer) {
    exec_memory *memory = caller ? caller->memory : NULL;
    int descriptors[2];
    uint8_t encoded[8];
    int status;
    if (!store || !store->kernel || !memory) return -POSIX_EINVAL;
    status = posix_kernel_pipe(store->kernel, descriptors);
    if (status < 0) return status;
    for (int at = 0; at < 2; at++) {
        uint32_t value = (uint32_t)descriptors[at];
        encoded[at * 4 + 0] = (uint8_t)value;
        encoded[at * 4 + 1] = (uint8_t)(value >> 8);
        encoded[at * 4 + 2] = (uint8_t)(value >> 16);
        encoded[at * 4 + 3] = (uint8_t)(value >> 24);
    }
    if (!native_posix_write_guest(memory, descriptor_pointer,
                                  encoded, sizeof(encoded))) {
        (void)posix_kernel_close(store->kernel, descriptors[0]);
        (void)posix_kernel_close(store->kernel, descriptors[1]);
        return -POSIX_EFAULT;
    }
    return 0;
}

static exec_status native_posix_pipe_v1(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count, exec_error *error,
                                        const waste_exec_engine *caller) {
    int result = arg_count == 1 ? native_posix_pipe_create(
        (native_store *)data, caller, (uint32_t)args[0].i32) : -POSIX_EINVAL;
    (void)error;
    return native_posix_result(result, results, result_count);
}

static exec_status native_posix_pipe(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error,
                                     const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    int result = arg_count == 1 ? native_posix_pipe_create(
        store, caller, (uint32_t)args[0].i32) : -POSIX_EINVAL;
    if (result < 0) {
        native_posix_set_errno(store, caller, -result);
        result = -1;
    }
    (void)error;
    return native_posix_result(result, results, result_count);
}

static exec_status native_posix_dup2(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error,
                                     const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    int result;
    (void)error;
    if (!store || !store->kernel || arg_count != 2) {
        native_posix_set_errno(store, caller, POSIX_EINVAL);
        return native_posix_result(-1, results, result_count);
    }
    result = posix_kernel_dup2(store->kernel, args[0].i32, args[1].i32);
    if (result < 0) {
        native_posix_set_errno(store, caller, -result);
        result = -1;
    }
    return native_posix_result(result, results, result_count);
}

static exec_status native_posix_ftruncate(void *data, const wasm_value *args,
                                          int arg_count, wasm_value *results,
                                          int *result_count, exec_error *error,
                                          const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    int status;
    (void)error;
    if (!store || arg_count != 2 || args[1].i64 < 0 ||
        (uint64_t)args[1].i64 > SIZE_MAX) {
        native_posix_set_errno(store, caller, POSIX_EINVAL);
        return native_posix_result(-1, results, result_count);
    }
    status = posix_kernel_ftruncate(store->kernel, args[0].i32,
                                    (uint64_t)args[1].i64);
    if (status < 0) native_posix_set_errno(store, caller, -status);
    return native_posix_result(status, results, result_count);
}

static exec_status native_posix_read(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error,
                                     const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    uint8_t *buffer = NULL;
    uint32_t count;
    if (arg_count != 3 || native_posix_memory(caller, &memory, error) != EXEC_OK)
        return error->status;
    count = (uint32_t)args[2].i32;
    if ((count && !(buffer = (uint8_t *)malloc(count))) ||
        exec_memory_read(memory, (uint32_t)args[1].i32, buffer, count, error) != EXEC_OK) {
        free(buffer);
        error->status = EXEC_ERROR_TRAP;
        snprintf(error->message, sizeof(error->message),
                 "POSIX read buffer is outside guest memory");
        return error->status;
    }
    int32_t read_result;
    if (store->kernel_terminal) {
        read_result = posix_kernel_read(store->kernel, args[0].i32,
                                        buffer, (int)count);
        if (read_result == -POSIX_EAGAIN) {
            free(buffer);
            waste_browser_record_transition("read-eagain");
            error->yield_reason = EXEC_YIELD_READ;
            return EXEC_YIELD;
        }
        if (read_result >= 0)
            waste_browser_record_transition("read-data");
        else
            waste_browser_record_transition("read-error");
    } else if (store->kernel) {
        read_result = posix_kernel_read(store->kernel, args[0].i32,
                                        buffer, (int)count);
    } else {
        /* Noninteractive WAST sandboxes retain the narrow host capability. */
        read_result = waste_host_posix_read(args[0].i32, buffer, count);
        if (read_result == -2) {
            free(buffer);
            error->yield_reason = EXEC_YIELD_READ;
            return EXEC_YIELD;
        }
    }
    if (read_result > 0 && exec_memory_write(
            memory, (uint32_t)args[1].i32, buffer, (size_t)read_result,
            error) != EXEC_OK) {
        free(buffer);
        return error->status;
    }
    free(buffer);
    return native_posix_result(read_result, results, result_count);
}

static exec_status native_posix_write(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    uint8_t *buffer = NULL;
    uint32_t count;
    int32_t terminal_failure = -POSIX_EFAULT;
    if (arg_count != 3 || native_posix_memory(caller, &memory, error) != EXEC_OK)
        return error->status;
    count = (uint32_t)args[2].i32;
    if ((count && !(buffer = (uint8_t *)malloc(count))) ||
        exec_memory_read(memory, (uint32_t)args[1].i32, buffer, count, error) != EXEC_OK) {
        free(buffer);
        error->status = EXEC_ERROR_TRAP;
        snprintf(error->message, sizeof(error->message),
                 "POSIX write buffer is outside guest memory");
        return error->status;
    }
    if (store && store->kernel_terminal &&
        posix_kernel_isatty(store->kernel, args[0].i32)) {
        uint8_t translated[512];
        uint32_t offset = 0;
        while (offset < count) {
            uint32_t chunk = count - offset;
            if (chunk > 256) chunk = 256;
            int translated_count = posix_kernel_terminal_process_output(
                store->kernel, args[0].i32, buffer + offset, (int)chunk,
                translated, (int)sizeof(translated));
            if (translated_count < 0) {
                terminal_failure = translated_count;
                goto terminal_error;
            }
            int32_t written = waste_host_posix_write(
                args[0].i32, translated, (uint32_t)translated_count);
            char write_event[32];
            int has_bash_prompt = 0;
            for (int i = 0; i + 4 < translated_count; i++)
                if (translated[i] == 'b' && translated[i + 1] == 'a' &&
                    translated[i + 2] == 's' && translated[i + 3] == 'h' &&
                    translated[i + 4] == '-') {
                    has_bash_prompt = 1;
                    break;
                }
            snprintf(write_event, sizeof(write_event), "term-write-n%d%s",
                     translated_count, has_bash_prompt ? "-bash" : "");
            waste_browser_record_transition(write_event);
            if (written < 0) {
                terminal_failure = written;
                goto terminal_error;
            }
            if (written != translated_count)
                { int32_t partial = (int32_t)offset; free(buffer);
                  return native_posix_result(partial, results, result_count); }
            offset += chunk;
        }
        free(buffer);
        return native_posix_result((int32_t)count, results, result_count);
    }
    int32_t write_result = store && store->kernel ?
        posix_kernel_write(store->kernel, args[0].i32, buffer, (int)count) :
        waste_host_posix_write(args[0].i32, buffer, count);
    free(buffer);
    return native_posix_result(write_result, results, result_count);

terminal_error:
    free(buffer);
    return native_posix_result(terminal_failure, results, result_count);
}

static exec_status native_posix_i32_zero(void *data, const wasm_value *args,
                                         int arg_count, wasm_value *results,
                                         int *result_count,
                                         exec_error *error,
                                         const waste_exec_engine *caller) {
    (void)data; (void)args; (void)arg_count; (void)error; (void)caller;
    return native_posix_result(0, results, result_count);
}

static exec_status native_posix_i32_one(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count,
                                        exec_error *error,
                                        const waste_exec_engine *caller) {
    (void)data; (void)args; (void)arg_count; (void)error; (void)caller;
    return native_posix_result(1, results, result_count);
}

static exec_status native_posix_getpgrp(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count, exec_error *error,
                                        const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    (void)caller; (void)error;
    if (!store || arg_count != 0)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    return native_posix_result(posix_kernel_getpgid(store->kernel), results,
                               result_count);
}

static exec_status native_posix_setpgid(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count, exec_error *error,
                                        const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    int target_pid;
    (void)caller; (void)error;
    if (!store || arg_count != 2 || args[0].i32 < 0 || args[1].i32 <= 0)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    target_pid = args[0].i32 == 0 ? native_store_getpid(store) : args[0].i32;
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++) {
        if (store->processes[i].used && store->processes[i].pid == target_pid)
            return native_posix_result(posix_kernel_setpgid(
                                           store->processes[i].kernel,
                                           args[1].i32), results, result_count);
    }
    return native_posix_result(-POSIX_EINVAL, results, result_count);
}

static exec_status native_posix_tcgetpgrp(void *data, const wasm_value *args,
                                          int arg_count, wasm_value *results,
                                          int *result_count, exec_error *error,
                                          const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    (void)caller; (void)error;
    if (!store || arg_count != 1)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    return native_posix_result(posix_kernel_terminal_get_foreground_pgid(
                                   store->kernel, args[0].i32),
                               results, result_count);
}

static exec_status native_posix_tcsetpgrp(void *data, const wasm_value *args,
                                          int arg_count, wasm_value *results,
                                          int *result_count, exec_error *error,
                                          const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    (void)caller; (void)error;
    if (!store || arg_count != 2)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    return native_posix_result(posix_kernel_terminal_set_foreground_pgid(
                                   store->kernel, args[0].i32, args[1].i32),
                               results, result_count);
}

/* Stable guest sigaction prefix: handler pointer at offset zero, followed by
 * a fixed-width 128-bit mask. Flags remain outside this ABI prefix. */
static exec_status native_posix_sigaction(void *data, const wasm_value *args,
                                          int arg_count, wasm_value *results,
                                          int *result_count, exec_error *error,
                                          const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    if (!store || !store->kernel || arg_count != 3)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    int signal = args[0].i32;
    uint32_t action_ptr = (uint32_t)args[1].i32;
    uint32_t old_ptr = (uint32_t)args[2].i32;
    uint32_t old_handler = POSIX_SIG_DFL;
    posix_sigset old_mask = {{0, 0, 0, 0}};
    if (posix_kernel_signal_get_handler(store->kernel, signal, &old_handler) < 0)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    if (posix_kernel_signal_get_action_mask(store->kernel, signal, &old_mask) < 0)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    if (old_ptr) {
        exec_memory *memory;
        uint8_t bytes[sizeof(uint32_t) + POSIX_SIGSET_BYTES];
        if (native_posix_memory(caller, &memory, error) != EXEC_OK)
            return native_posix_result(-POSIX_EFAULT, results, result_count);
        memcpy(bytes, &old_handler, sizeof(old_handler));
        memcpy(bytes + sizeof(old_handler), &old_mask, POSIX_SIGSET_BYTES);
        if (!native_posix_write_guest(memory, old_ptr, bytes, sizeof(bytes)))
            return native_posix_result(-POSIX_EFAULT, results, result_count);
    }
    if (action_ptr) {
        exec_memory *memory;
        uint8_t bytes[sizeof(uint32_t) + POSIX_SIGSET_BYTES];
        uint32_t handler;
        posix_sigset action_mask;
        if (native_posix_memory(caller, &memory, error) != EXEC_OK ||
            !native_posix_read_guest(memory, action_ptr, bytes, sizeof(bytes)))
            return native_posix_result(-POSIX_EFAULT, results, result_count);
        memcpy(&handler, bytes, sizeof(handler));
        memcpy(&action_mask, bytes + sizeof(handler), POSIX_SIGSET_BYTES);
        posix_signal_disposition disposition = POSIX_SIGNAL_HANDLER;
        if (handler == POSIX_SIG_DFL) disposition = POSIX_SIGNAL_DEFAULT;
        else if (handler == POSIX_SIG_IGN || handler == UINT32_C(1))
            disposition = POSIX_SIGNAL_IGNORE;
        if (posix_kernel_signal_set_disposition(store->kernel, signal,
                                                disposition) < 0)
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_kernel_signal_set_handler(store->kernel, signal, handler);
        posix_kernel_signal_set_action_mask(store->kernel, signal, &action_mask);
    }
    return native_posix_result(0, results, result_count);
}

static exec_status native_posix_isatty(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    (void)error; (void)caller;
    if (arg_count != 1) return native_posix_result(-POSIX_EINVAL, results, result_count);
    native_store *store = (native_store *)data;
    return native_posix_result(store->kernel &&
        posix_kernel_isatty(store->kernel, args[0].i32) ? 1 : 0,
        results, result_count);
}

static exec_status native_posix_tcgetattr(void *data, const wasm_value *args,
                                          int arg_count, wasm_value *results,
                                          int *result_count, exec_error *error,
                                          const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = NULL;
    uint8_t bytes[sizeof(posix_termios)];
    posix_termios termios;
    if (arg_count != 2 || !store->kernel ||
        native_posix_memory(caller, &memory, error) != EXEC_OK ||
        posix_kernel_tcgetattr(store->kernel, args[0].i32, &termios) < 0)
        return native_posix_result(-POSIX_EBADF, results, result_count);
    memcpy(bytes, &termios, sizeof(termios));
    if (!native_posix_write_guest(memory, (uint32_t)args[1].i32,
                                  bytes, sizeof(bytes)))
        return native_posix_result(-POSIX_EFAULT, results, result_count);
    return native_posix_result(0, results, result_count);
}

static exec_status native_posix_tcsetattr(void *data, const wasm_value *args,
                                          int arg_count, wasm_value *results,
                                          int *result_count, exec_error *error,
                                          const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = NULL;
    uint8_t bytes[sizeof(posix_termios)];
    posix_termios termios;
    if (arg_count != 3 || !store->kernel ||
        native_posix_memory(caller, &memory, error) != EXEC_OK ||
        !native_posix_read_guest(memory, (uint32_t)args[2].i32,
                                 bytes, sizeof(bytes)))
        return native_posix_result(-POSIX_EFAULT, results, result_count);
    memcpy(&termios, bytes, sizeof(termios));
    return native_posix_result(posix_kernel_tcsetattr(store->kernel, args[0].i32,
                                                       &termios),
                               results, result_count);
}

static exec_status native_posix_ioctl(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = NULL;
    if (arg_count != 3 || !store->kernel ||
        native_posix_memory(caller, &memory, error) != EXEC_OK)
        return native_posix_result(-POSIX_EFAULT, results, result_count);
    if (args[1].i32 == POSIX_TIOCGWINSZ) {
        posix_winsize winsize;
        if (posix_kernel_terminal_get_winsize(store->kernel, args[0].i32,
                                               &winsize) < 0)
            return native_posix_result(-POSIX_EBADF, results, result_count);
        if (!native_posix_write_guest(memory, (uint32_t)args[2].i32,
                                      &winsize, sizeof(winsize)))
            return native_posix_result(-POSIX_EFAULT, results, result_count);
        return native_posix_result(0, results, result_count);
    }
    if (args[1].i32 == POSIX_TIOCSWINSZ) {
        posix_winsize winsize;
        if (!native_posix_read_guest(memory, (uint32_t)args[2].i32,
                                     &winsize, sizeof(winsize)))
            return native_posix_result(-POSIX_EFAULT, results, result_count);
        return native_posix_result(posix_kernel_terminal_set_winsize(
            store->kernel, args[0].i32, &winsize), results, result_count);
    }
    return native_posix_result(-POSIX_EINVAL, results, result_count);
}

static exec_status native_posix_tcflow(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    (void)error; (void)caller;
    native_store *store = (native_store *)data;
    if (arg_count != 2 || !store->kernel)
        return native_posix_result(-POSIX_EFAULT, results, result_count);
    return native_posix_result(posix_kernel_tcflow(store->kernel,
                                                   args[0].i32, args[1].i32),
                                results, result_count);
}

static exec_status native_posix_i32_sixty_four(
        void *data, const wasm_value *args, int arg_count,
        wasm_value *results, int *result_count, exec_error *error,
        const waste_exec_engine *caller) {
    (void)data; (void)args; (void)arg_count; (void)error; (void)caller;
    return native_posix_result(64, results, result_count);
}

static exec_status native_posix_i32_negative(void *data,
                                             const wasm_value *args,
                                             int arg_count,
                                             wasm_value *results,
                                             int *result_count,
                                             exec_error *error,
                                             const waste_exec_engine *caller) {
    (void)data; (void)args; (void)arg_count; (void)error; (void)caller;
    return native_posix_result(-1, results, result_count);
}

static exec_status native_posix_i64_zero(void *data, const wasm_value *args,
                                         int arg_count, wasm_value *results,
                                         int *result_count,
                                         exec_error *error,
                                         const waste_exec_engine *caller) {
    (void)data; (void)args; (void)arg_count; (void)error; (void)caller;
    results[0].type = WASM_VALTYPE_I64;
    results[0].i64 = 0;
    *result_count = 1;
    return EXEC_OK;
}

static exec_status native_posix_i64_negative(void *data,
                                             const wasm_value *args,
                                             int arg_count,
                                             wasm_value *results,
                                             int *result_count,
                                             exec_error *error,
                                             const waste_exec_engine *caller) {
    (void)data; (void)args; (void)arg_count; (void)error; (void)caller;
    results[0].type = WASM_VALTYPE_I64;
    results[0].i64 = -1;
    *result_count = 1;
    return EXEC_OK;
}

static exec_status native_posix_void(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error,
                                     const waste_exec_engine *caller) {
    (void)data; (void)args; (void)arg_count; (void)results; (void)error;
    (void)caller;
    *result_count = 0;
    return EXEC_OK;
}

static exec_status native_posix_exit(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error,
                                     const waste_exec_engine *caller) {
    (void)data; (void)results; (void)caller;
    if (arg_count != 1) return native_posix_result(-POSIX_EINVAL, results,
                                                   result_count);
    if (error) {
        error->status = EXEC_ERROR_EXIT;
        error->exit_code = args[0].i32 & 0xff;
    }
    *result_count = 0;
    return EXEC_ERROR_EXIT;
}

static exec_status native_posix_raise(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    int status;
    posix_signal_disposition disposition;
    (void)caller; (void)error;
    if (!store || arg_count != 1)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    status = posix_kernel_signal_raise(store->kernel, args[0].i32);
    if (status == 0 && posix_kernel_signal_get_disposition(
            store->kernel, args[0].i32, &disposition) == 0 &&
        disposition == POSIX_SIGNAL_DEFAULT && args[0].i32 != POSIX_SIGSTOP) {
        if (error) {
            error->status = EXEC_ERROR_EXIT;
            error->exit_code = 128 + args[0].i32;
        }
        return EXEC_ERROR_EXIT;
    }
    return native_posix_result(status, results, result_count);
}

static exec_status native_posix_kill(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error,
                                     const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    int pid;
    (void)caller; (void)error;
    if (!store || arg_count != 2 || args[0].i32 < 0)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    pid = args[0].i32 == 0 ? native_store_getpid(store) : args[0].i32;
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++) {
        if (store->processes[i].used && store->processes[i].pid == pid)
            return native_posix_result(native_store_signal_process(
                                           store, pid, args[1].i32), results,
                                       result_count);
    }
    return native_posix_result(-POSIX_EINVAL, results, result_count);
}

static exec_status native_posix_killpg(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    int pgid, delivered = 0;
    (void)caller; (void)error;
    if (!store || arg_count != 2 || args[0].i32 < 0)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    pgid = args[0].i32 == 0 ? posix_kernel_getpgid(store->kernel) : args[0].i32;
    for (int i = 0; i < NATIVE_PROCESS_MAX; i++) {
        if (store->processes[i].used &&
            posix_kernel_getpgid(store->processes[i].kernel) == pgid) {
            (void)native_store_signal_process(store,
                                               store->processes[i].pid,
                                               args[1].i32);
            delivered = 1;
        }
    }
    return native_posix_result(delivered ? 0 : -POSIX_EINVAL, results,
                               result_count);
}

static exec_status native_posix_fcntl(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    int fd, command;
    (void)error; (void)caller;
    if (!store || arg_count < 2) return native_posix_result(-POSIX_EINVAL,
                                                             results, result_count);
    fd = args[0].i32;
    command = args[1].i32;
    if (command == POSIX_F_DUPFD && arg_count >= 3)
        return native_posix_result(posix_kernel_dupfd(
                                       store->kernel, fd, args[2].i32, 0),
                                   results, result_count);
    if (command == POSIX_F_DUPFD_CLOEXEC && arg_count >= 3)
        return native_posix_result(posix_kernel_dupfd(
                                       store->kernel, fd, args[2].i32, 1),
                                   results, result_count);
    if (command == POSIX_F_GETFD)
        return native_posix_result(posix_kernel_get_cloexec(store->kernel, fd),
                                   results, result_count);
    if (command == POSIX_F_SETFD && arg_count >= 3)
        return native_posix_result(posix_kernel_set_cloexec(
                                       store->kernel, fd, args[2].i32),
                                   results, result_count);
    return native_posix_result(-POSIX_EINVAL, results, result_count);
}

static exec_status native_posix_getcwd(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count,
                                       exec_error *error,
                                       const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    uint8_t *buffer;
    uint32_t offset;
    uint32_t capacity;
    if (arg_count != 2 ||
        native_posix_memory(caller, &memory, error) != EXEC_OK)
        return native_posix_result(0, results, result_count);
    offset = (uint32_t)args[0].i32;
    capacity = (uint32_t)args[1].i32;
    if (offset == 0) {
        uint32_t malloc_index;
        wasm_value malloc_arg;
        wasm_value malloc_result;
        int malloc_result_count = 0;
        /* The returned pointer belongs to the calling image.  Allocating from
         * the registered env module produces an address in a different Wasm
         * linear memory, which makes getcwd(NULL, 0) unusable for exec'd
         * utilities such as pwd. */
        if (!caller || exec_find_export(caller, "malloc", &malloc_index,
                                        error) != EXEC_OK)
            return native_posix_result(0, results, result_count);
        malloc_arg.type = WASM_VALTYPE_I32;
        /* POSIX getcwd(NULL, 0) asks libc to allocate a result large enough
         * for the current directory. The old two-byte placeholder forced the
         * guest fallback path and could recurse through gnulib's getcwd
         * implementation. Use the engine's bounded pathname limit. */
        malloc_arg.i32 = POSIX_PATH_MAX;
        if (exec_invoke((waste_exec_engine *)caller, malloc_index, &malloc_arg, 1,
                        &malloc_result, &malloc_result_count, error) != EXEC_OK ||
            malloc_result_count != 1)
            return native_posix_result(0, results, result_count);
        offset = (uint32_t)malloc_result.i32;
        capacity = POSIX_PATH_MAX;
    }
    if (capacity < 2)
        return native_posix_result(0, results, result_count);
    buffer = (uint8_t *)malloc(capacity);
    if (!buffer)
        return native_posix_result(0, results, result_count);
    if (!store->kernel || posix_kernel_getcwd(store->kernel, (char *)buffer,
                                               capacity) < 0) {
        free(buffer);
        return native_posix_result(0, results, result_count);
    }
    if (!native_posix_write_guest(memory, offset, buffer, capacity)) {
        free(buffer);
        return native_posix_result(0, results, result_count);
    }
    free(buffer);
    return native_posix_result((int32_t)offset, results, result_count);
}

static exec_status native_posix_chdir(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    char *path = NULL;
    size_t length;
    int errno_value = 0;
    if (arg_count != 1 || !native_posix_guest_path(caller, (uint32_t)args[0].i32,
                                                   &path, &length, &errno_value))
        return native_posix_result(-1, results, result_count);
    int result = store->kernel ? posix_kernel_path_set_cwd(
        store->kernel, path) : -POSIX_ENOSYS;
    if (result < 0) native_posix_set_errno(store, caller, -result);
    free(path);
    (void)error;
    return native_posix_result(result < 0 ? -1 : 0, results, result_count);
}

static exec_status native_posix_fchdir(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    if (arg_count < 1)
        return native_posix_result(-1, results, result_count);
    int result = store->kernel ? posix_kernel_fchdir(store->kernel, args[0].i32)
                               : -POSIX_ENOSYS;
    if (result < 0) native_posix_set_errno(store, caller, -result);
    (void)error;
    return native_posix_result(result < 0 ? -1 : 0, results, result_count);
}

static exec_status native_posix_mkdir(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    char *path = NULL; size_t length; int errno_value = 0;
    if (arg_count != 2 || !native_posix_guest_path(caller, (uint32_t)args[0].i32,
                                                   &path, &length, &errno_value))
        return native_posix_result(-1, results, result_count);
    int result = store->kernel ? posix_kernel_path_mkdir(
        store->kernel, (const uint8_t *)path, length, args[1].i32) : -POSIX_ENOSYS;
    if (result < 0) native_posix_set_errno(store, caller, -result);
    free(path);
    (void)error;
    return native_posix_result(result < 0 ? -1 : 0, results, result_count);
}

static exec_status native_posix_unlink(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    char *path = NULL; size_t length; int errno_value = 0;
    if (arg_count != 1 || !native_posix_guest_path(caller, (uint32_t)args[0].i32,
                                                   &path, &length, &errno_value))
        return native_posix_result(-1, results, result_count);
    int result = store->kernel ? posix_kernel_path_unlink(
        store->kernel, (const uint8_t *)path, length, 0) : -POSIX_ENOSYS;
    if (result < 0) native_posix_set_errno(store, caller, -result);
    free(path);
    (void)error;
    return native_posix_result(result < 0 ? -1 : 0, results, result_count);
}

static exec_status native_posix_rename(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    char *source = NULL, *destination = NULL;
    size_t source_length, destination_length;
    int errno_value = 0;
    if (arg_count != 2 || !native_posix_guest_path(caller, (uint32_t)args[0].i32,
                                                   &source, &source_length, &errno_value) ||
        !native_posix_guest_path(caller, (uint32_t)args[1].i32,
                                 &destination, &destination_length, &errno_value)) {
        free(source);
        free(destination);
        return native_posix_result(-1, results, result_count);
    }
    int result = store->kernel ? posix_kernel_path_rename(
        store->kernel, (const uint8_t *)source, source_length,
        (const uint8_t *)destination, destination_length)
        : -POSIX_ENOSYS;
    if (result < 0) native_posix_set_errno(store, caller, -result);
    free(source);
    free(destination);
    (void)error;
    return native_posix_result(result < 0 ? -1 : 0, results, result_count);
}

static exec_status native_posix_readdir(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count, exec_error *error,
                                        const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    uint8_t *name_buffer;
    uint8_t metadata_buffer[POSIX_PATH_METADATA_BYTES];
    posix_path_metadata metadata;
    if (arg_count != 4 || native_posix_memory(caller, &memory, error) != EXEC_OK)
        return native_posix_result(-POSIX_EFAULT, results, result_count);
    name_buffer = (uint8_t *)malloc((size_t)args[2].i32);
    if (!name_buffer || !native_posix_read_guest(memory, (uint32_t)args[3].i32,
                                                 metadata_buffer,
                                                 sizeof(metadata_buffer))) {
        free(name_buffer);
        return native_posix_result(-POSIX_EFAULT, results, result_count);
    }
    int result = store->kernel ? posix_kernel_readdir(
        store->kernel, args[0].i32, (char *)name_buffer, (size_t)args[2].i32,
        &metadata) : -POSIX_ENOSYS;
    if (result > 0) {
        posix_path_metadata_encode(metadata_buffer, &metadata);
        if (!native_posix_write_guest(memory, (uint32_t)args[1].i32,
                                      name_buffer, (size_t)args[2].i32) ||
            !native_posix_write_guest(memory, (uint32_t)args[3].i32,
                                      metadata_buffer, sizeof(metadata_buffer)))
            result = -POSIX_EFAULT;
    }
    free(name_buffer);
    if (result < 0) native_posix_set_errno(store, caller, -result);
    return native_posix_result(result, results, result_count);
}

static exec_status native_posix_readlink(void *data, const wasm_value *args,
                                         int arg_count, wasm_value *results,
                                         int *result_count, exec_error *error,
                                         const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    char *path = NULL; size_t path_length; uint8_t *buffer;
    int errno_value = 0;
    if (arg_count != 3 || native_posix_memory(caller, &memory, error) != EXEC_OK ||
        !native_posix_guest_path(caller, (uint32_t)args[0].i32, &path,
                                 &path_length, &errno_value))
        return native_posix_result(-POSIX_EFAULT, results, result_count);
    buffer = (uint8_t *)malloc((size_t)args[2].i32);
    if (!buffer) {
        free(path);
        return native_posix_result(-POSIX_ENOMEM, results, result_count);
    }
    int result = store->kernel ? posix_kernel_path_readlink(
        store->kernel, (const uint8_t *)path, path_length,
        (char *)buffer, (size_t)args[2].i32)
        : -POSIX_ENOSYS;
    if (result > 0 && !native_posix_write_guest(memory, (uint32_t)args[1].i32,
                                                buffer, (size_t)result))
        result = -POSIX_EFAULT;
    if (result < 0) native_posix_set_errno(store, caller, -result);
    free(buffer);
    free(path);
    return native_posix_result(result, results, result_count);
}

static exec_status native_posix_rmdir(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    char *path = NULL; size_t length; int errno_value = 0;
    if (arg_count != 1 || !native_posix_guest_path(caller, (uint32_t)args[0].i32,
                                                   &path, &length, &errno_value))
        return native_posix_result(-1, results, result_count);
    int result = store->kernel ? posix_kernel_path_unlink(
        store->kernel, (const uint8_t *)path, length, 1) : -POSIX_ENOSYS;
    if (result < 0) native_posix_set_errno(store, caller, -result);
    free(path);
    (void)error;
    return native_posix_result(result < 0 ? -1 : 0, results, result_count);
}

static exec_status native_posix_lseek(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    int64_t result_value = -1;
    int result = arg_count == 3 && store->kernel ? posix_kernel_lseek(
        store->kernel, args[0].i32, args[1].i64, args[2].i32, &result_value)
        : -POSIX_EINVAL;
    if (result < 0) {
        native_posix_set_errno(store, caller, -result);
        result_value = -1;
    }
    results[0].type = WASM_VALTYPE_I64;
    results[0].i64 = result_value;
    *result_count = 1;
    (void)error;
    return EXEC_OK;
}

static exec_status native_posix_stat_common(void *data, const wasm_value *args,
                                            int arg_count, wasm_value *results,
                                            int *result_count, exec_error *error,
                                            int follow,
                                            const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    char *path = NULL;
    size_t path_length;
    uint8_t status[POSIX_GUEST_STAT_BYTES];
    posix_path_metadata metadata;
    posix_guest_stat guest_stat;
    int errno_value = POSIX_ENOSYS;
    if (arg_count != 2 || native_posix_memory(caller, &memory, error) != EXEC_OK)
        return native_posix_result(-1, results, result_count);
    if (!native_posix_guest_path(caller, (uint32_t)args[0].i32, &path,
                                 &path_length, &errno_value) ||
        !native_posix_read_guest(memory, (uint32_t)args[1].i32,
                                 status, sizeof(status))) {
        native_posix_set_errno(store, caller, errno_value ? errno_value : POSIX_EFAULT);
        return native_posix_result(-1, results, result_count);
    }
    int result = store->kernel ? posix_kernel_path_stat(
        store->kernel, (const uint8_t *)path, path_length,
        follow, &metadata) : -POSIX_ENOSYS;
    if (result < 0) {
        native_posix_set_errno(store, caller, -result);
        free(path);
        return native_posix_result(-1, results, result_count);
    }
    native_posix_metadata_stat(&metadata, &guest_stat);
    posix_guest_stat_encode(status, &guest_stat);
    if (!native_posix_write_guest(memory, (uint32_t)args[1].i32,
                                  status, sizeof(status))) {
        free(path);
        native_posix_set_errno(store, caller, POSIX_EFAULT);
        return native_posix_result(-1, results, result_count);
    }
    free(path);
    return native_posix_result(0, results, result_count);
}

static exec_status native_posix_stat(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error,
                                     const waste_exec_engine *caller) {
    return native_posix_stat_common(data, args, arg_count, results,
                                    result_count, error, 1, caller);
}

static exec_status native_posix_lstat(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    return native_posix_stat_common(data, args, arg_count, results,
                                    result_count, error, 0, caller);
}

static exec_status native_posix_access(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    char *path = NULL;
    size_t path_length;
    int errno_value = 0;
    if (arg_count != 2 || !native_posix_guest_path(
            caller, (uint32_t)args[0].i32, &path, &path_length, &errno_value)) {
        native_posix_set_errno(store, caller, errno_value ? errno_value : POSIX_EFAULT);
        return native_posix_result(-1, results, result_count);
    }
    int result = store->kernel ? posix_kernel_path_access(
        store->kernel, (const uint8_t *)path, path_length,
        args[1].i32, 0) : -POSIX_ENOSYS;
    if (result < 0) native_posix_set_errno(store, caller, -result);
    free(path);
    (void)error;
    return native_posix_result(result < 0 ? -1 : 0, results, result_count);
}

static exec_status native_posix_faccessat(void *data, const wasm_value *args,
                                          int arg_count, wasm_value *results,
                                          int *result_count, exec_error *error,
                                          const waste_exec_engine *caller) {
    if (arg_count != 4) return native_posix_result(-1, results, result_count);
    /* The initial namespace has no dirfd-relative escape; AT_FDCWD is the
     * only accepted base and is represented by -100. */
    if (args[0].i32 != -100) {
        native_posix_set_errno((native_store *)data, caller, POSIX_EBADF);
        return native_posix_result(-1, results, result_count);
    }
    wasm_value access_args[2] = { args[1], args[2] };
    return native_posix_access(data, access_args, 2, results, result_count,
                               error, caller);
}

static exec_status native_posix_fstat(void *data, const wasm_value *args,
                                      int arg_count, wasm_value *results,
                                      int *result_count, exec_error *error,
                                      const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    uint8_t status[POSIX_GUEST_STAT_BYTES];
    posix_guest_stat guest_stat;
    if (arg_count != 2 || native_posix_memory(caller, &memory, error) != EXEC_OK ||
        !native_posix_read_guest(memory, (uint32_t)args[1].i32,
                                 status, sizeof(status))) {
        native_posix_set_errno(store, caller, POSIX_EFAULT);
        return native_posix_result(-1, results, result_count);
    }
    if (!store->kernel || args[0].i32 < 0 || args[0].i32 >= POSIX_KERNEL_FD_MAX ||
        !store->kernel->fds[args[0].i32].ofd) {
        native_posix_set_errno(store, caller, POSIX_EBADF);
        return native_posix_result(-1, results, result_count);
    }
    posix_path_metadata metadata = {
        POSIX_NODE_REGULAR, 0666, 0, 0, 0, (uint64_t)(args[0].i32 + 1)
    };
    native_posix_metadata_stat(&metadata, &guest_stat);
    posix_guest_stat_encode(status, &guest_stat);
    if (!native_posix_write_guest(memory, (uint32_t)args[1].i32,
                                  status, sizeof(status))) {
        native_posix_set_errno(store, caller, POSIX_EFAULT);
        return native_posix_result(-1, results, result_count);
    }
    return native_posix_result(0, results, result_count);
}

static exec_status native_posix_fork(void *data, const wasm_value *args,
                                     int arg_count, wasm_value *results,
                                     int *result_count, exec_error *error,
                                     const waste_exec_engine *caller) {
    (void)args; (void)arg_count; (void)caller;
    native_store *store = (native_store *)data;
    native_process_capsule *capsule = native_store_active_capsule(store);
    if (capsule && capsule->pending_result_valid) {
        int result = 0;
        if (native_store_take_process_wake(store, &result) == 0)
            return native_posix_result(result, results, result_count);
    }
    if (store->fork_child_resume) {
        store->fork_child_resume = 0;
        return native_posix_result(0, results, result_count);
    }
    if (store->fork_parent_resume) {
        int pid = store->fork_child_pid;
        store->fork_parent_resume = 0;
        return native_posix_result(pid, results, result_count);
    }
    if (error) {
        memset(error, 0, sizeof(*error));
        error->status = EXEC_YIELD;
        error->yield_reason = EXEC_YIELD_FORK;
    }
    return EXEC_YIELD;
}

static exec_status native_posix_waitpid(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count, exec_error *error,
                                        const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = NULL;
    uint8_t status_bytes[4];
    if (arg_count != 3 || native_posix_memory(caller, &memory, error) != EXEC_OK) {
        native_posix_set_errno(store, caller, POSIX_EFAULT);
        return native_posix_result(-1, results, result_count);
    }
    int status = 0;
    int waited = native_store_wait_process(store, args[0].i32, args[2].i32,
                                           &status);
    if (waited < 0) {
        if (waited == -POSIX_ECHILD && args[0].i32 == 0 &&
            store->last_wait_pid > 0) {
            status = store->last_wait_status;
            status_bytes[0] = (uint8_t)status;
            status_bytes[1] = (uint8_t)(status >> 8);
            status_bytes[2] = (uint8_t)(status >> 16);
            status_bytes[3] = (uint8_t)(status >> 24);
            if (!native_posix_write_guest(memory, (uint32_t)args[1].i32,
                                          status_bytes, sizeof(status_bytes)))
                return native_posix_result(-POSIX_EFAULT, results, result_count);
            return native_posix_result(store->last_wait_pid, results, result_count);
        }
        native_posix_set_errno(store, caller, -waited);
        return native_posix_result(-1, results, result_count);
    }
    status_bytes[0] = (uint8_t)status;
    status_bytes[1] = (uint8_t)(status >> 8);
    status_bytes[2] = (uint8_t)(status >> 16);
    status_bytes[3] = (uint8_t)(status >> 24);
    if (!native_posix_write_guest(memory, (uint32_t)args[1].i32,
                                  status_bytes, sizeof(status_bytes)))
        return native_posix_result(-POSIX_EFAULT, results, result_count);
    return native_posix_result(waited, results, result_count);
}

static exec_status native_posix_execve(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = NULL;
    char *path = NULL;
    size_t path_length = 0;
    int path_error = POSIX_EFAULT;
    int status;
    if (store->exec_request.active &&
        store->exec_request.pid == native_store_getpid(store) &&
        store->exec_request.failure_errno != 0) {
        int failure = store->exec_request.failure_errno;
        native_exec_request_destroy(&store->exec_request);
        native_posix_set_errno(store, caller, failure);
        return native_posix_result(-1, results, result_count);
    }
    if (arg_count != 3 || native_posix_memory(caller, &memory, error) != EXEC_OK ||
        !native_posix_guest_path(caller, (uint32_t)args[0].i32, &path,
                                 &path_length, &path_error)) {
        native_posix_set_errno(store, caller, path_error);
        return native_posix_result(-1, results, result_count);
    }
    if (path_length >= NATIVE_EXEC_PATH_MAX) {
        native_posix_set_errno(store, caller, POSIX_E2BIG);
        free(path);
        return native_posix_result(-1, results, result_count);
    }
    if (store->kernel) {
        int access = posix_kernel_path_access(
            store->kernel, (const uint8_t *)path, path_length,
            POSIX_X_OK, 0);
        if (access != 0 &&
            !(access == -POSIX_ENOENT &&
              native_store_find_executable(store, (const char *)path))) {
            native_posix_set_errno(store, caller, -access);
            free(path);
            return native_posix_result(-1, results, result_count);
        }
    } else if (!native_store_find_executable(store, (const char *)path)) {
        native_posix_set_errno(store, caller, POSIX_ENOENT);
        free(path);
        return native_posix_result(-1, results, result_count);
    }
    if (store->exec_request.active) {
        native_posix_set_errno(store, caller, POSIX_EBUSY);
        free(path);
        return native_posix_result(-1, results, result_count);
    }
    native_exec_request_destroy(&store->exec_request);
    memcpy(store->exec_request.path, path, path_length);
    store->exec_request.path[path_length] = '\0';
    status = native_posix_copy_guest_vector(
        memory, (uint32_t)args[1].i32, store->exec_request.argv,
        NATIVE_EXEC_ARG_MAX, &store->exec_request.argc, 0);
    if (status == 0)
        status = native_posix_copy_guest_vector(
            memory, (uint32_t)args[2].i32, store->exec_request.envp,
            NATIVE_EXEC_ENV_MAX, &store->exec_request.envc, 1);
    if (status != 0) {
        native_exec_request_destroy(&store->exec_request);
        native_posix_set_errno(store, caller, status);
        free(path);
        return native_posix_result(-1, results, result_count);
    }
    free(path);
    store->exec_request.pid = native_store_getpid(store);
    store->exec_request.active = 1;
    if (error) {
        memset(error, 0, sizeof(*error));
        error->status = EXEC_YIELD;
        error->yield_reason = EXEC_YIELD_EXEC;
    }
    return EXEC_YIELD;
}

static exec_status native_posix_path_access_v1(
        void *data, const wasm_value *args, int arg_count,
        wasm_value *results, int *result_count, exec_error *error,
        const waste_exec_engine *caller) {
    (void)error;
    if (arg_count != 4) return native_posix_result(-POSIX_EINVAL, results, result_count);
    exec_memory *memory = caller->memory;
    uint32_t length = (uint32_t)args[1].i32;
    uint8_t *path = length ? (uint8_t *)malloc(length) : NULL;
    if (!memory || (length && !path) || exec_memory_read(
            memory, (uint32_t)args[0].i32, path, length, error) != EXEC_OK) {
        free(path);
        return native_posix_result(-POSIX_EFAULT, results, result_count);
    }
    int result = ((native_store *)data)->kernel ? posix_kernel_path_access(
        ((native_store *)data)->kernel, path, length,
        args[2].i32, args[3].i32) : -POSIX_ENOSYS;
    free(path);
    return native_posix_result(result, results, result_count);
}

static exec_status native_posix_path_stat_v1(
        void *data, const wasm_value *args, int arg_count,
        wasm_value *results, int *result_count, exec_error *error,
        const waste_exec_engine *caller) {
    (void)error;
    if (arg_count != 4) return native_posix_result(-POSIX_EINVAL, results, result_count);
    exec_memory *memory = caller->memory;
    uint32_t length = (uint32_t)args[1].i32;
    uint8_t *path = length ? (uint8_t *)malloc(length) : NULL;
    uint8_t metadata_bytes[POSIX_PATH_METADATA_BYTES];
    if (!memory || (length && !path) || exec_memory_read(
            memory, (uint32_t)args[0].i32, path, length, error) != EXEC_OK ||
        exec_memory_read(memory, (uint32_t)args[3].i32, metadata_bytes,
                         sizeof(metadata_bytes), error) != EXEC_OK) {
        free(path);
        return native_posix_result(-POSIX_EFAULT, results, result_count);
    }
    posix_path_metadata metadata;
    int result = ((native_store *)data)->kernel ? posix_kernel_path_stat(
        ((native_store *)data)->kernel,
        path, length,
        args[2].i32, &metadata) : -POSIX_ENOSYS;
    if (result == 0) {
        posix_path_metadata_encode(metadata_bytes, &metadata);
        if (exec_memory_write(memory, (uint32_t)args[3].i32,
                              metadata_bytes, sizeof(metadata_bytes), error) != EXEC_OK)
            result = -POSIX_EFAULT;
    }
    free(path);
    return native_posix_result(result, results, result_count);
}

/* ---- Kernel select/pselect host imports ---- */

static exec_status native_posix_select(void *data, const wasm_value *args,
                                       int arg_count, wasm_value *results,
                                       int *result_count, exec_error *error,
                                       const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    if (arg_count != 5 ||
        native_posix_memory(caller, &memory, error) != EXEC_OK)
        return error->status;
    if (!store->kernel)
        return native_posix_result(-POSIX_EINVAL, results, result_count);

    int32_t nfds = args[0].i32;
    uint32_t read_ptr = (uint32_t)args[1].i32;
    uint32_t write_ptr = (uint32_t)args[2].i32;
    uint32_t except_ptr = (uint32_t)args[3].i32;
    uint32_t timeout_ptr = (uint32_t)args[4].i32;
    /* Validate and decode fd_sets. */
    posix_fd_set rds, wrs, exs;
    uint8_t rds_bytes[POSIX_FD_SET_BYTES];
    uint8_t wrs_bytes[POSIX_FD_SET_BYTES];
    uint8_t exs_bytes[POSIX_FD_SET_BYTES];
    uint8_t timeout_bytes[POSIX_TIMEVAL_BYTES];
    posix_fd_set *rp = (void *)0, *wp = (void *)0, *ep = (void *)0;
    if (read_ptr) {
        if (!native_posix_read_guest(memory, read_ptr, rds_bytes,
                                     sizeof(rds_bytes)))
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_fd_set_decode(&rds, rds_bytes);
        rp = &rds;
    }
    if (write_ptr) {
        if (!native_posix_read_guest(memory, write_ptr, wrs_bytes,
                                     sizeof(wrs_bytes)))
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_fd_set_decode(&wrs, wrs_bytes);
        wp = &wrs;
    }
    if (except_ptr) {
        if (!native_posix_read_guest(memory, except_ptr, exs_bytes,
                                     sizeof(exs_bytes)))
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_fd_set_decode(&exs, exs_bytes);
        ep = &exs;
    }

    /* Decode timeout. */
    const posix_timeval *tvp = (void *)0;
    posix_timeval tv;
    if (timeout_ptr) {
        if (!native_posix_read_guest(memory, timeout_ptr, timeout_bytes,
                                     sizeof(timeout_bytes)))
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_timeval_decode(&tv, timeout_bytes);
        tvp = &tv;
    }

    int32_t ret = posix_kernel_select(store->kernel, nfds, rp, wp, ep, tvp);

    if (ret == -POSIX_EAGAIN) {
        error->yield_reason = EXEC_YIELD_SELECT;
        return EXEC_YIELD;
    }

    /* Encode output sets back to guest memory on success. */
    if (ret >= 0) {
        if (rp) {
            posix_fd_set_encode(rds_bytes, rp);
            if (!native_posix_write_guest(memory, read_ptr, rds_bytes,
                                          sizeof(rds_bytes))) ret = -POSIX_EFAULT;
        }
        if (wp) {
            posix_fd_set_encode(wrs_bytes, wp);
            if (!native_posix_write_guest(memory, write_ptr, wrs_bytes,
                                          sizeof(wrs_bytes))) ret = -POSIX_EFAULT;
        }
        if (ep) {
            posix_fd_set_encode(exs_bytes, ep);
            if (!native_posix_write_guest(memory, except_ptr, exs_bytes,
                                          sizeof(exs_bytes))) ret = -POSIX_EFAULT;
        }
    }

    return native_posix_result(ret, results, result_count);
}

static exec_status native_posix_pselect(void *data, const wasm_value *args,
                                        int arg_count, wasm_value *results,
                                        int *result_count, exec_error *error,
                                        const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    exec_memory *memory = (void *)0;
    if (arg_count != 6 ||
        native_posix_memory(caller, &memory, error) != EXEC_OK)
        return error->status;
    if (!store->kernel)
        return native_posix_result(-POSIX_EINVAL, results, result_count);

    int32_t nfds = args[0].i32;
    uint32_t read_ptr = (uint32_t)args[1].i32;
    uint32_t write_ptr = (uint32_t)args[2].i32;
    uint32_t except_ptr = (uint32_t)args[3].i32;
    uint32_t timeout_ptr = (uint32_t)args[4].i32;
    uint32_t sigmask_ptr = (uint32_t)args[5].i32;
    /* Validate and decode fd_sets. */
    posix_fd_set rds, wrs, exs;
    uint8_t rds_bytes[POSIX_FD_SET_BYTES];
    uint8_t wrs_bytes[POSIX_FD_SET_BYTES];
    uint8_t exs_bytes[POSIX_FD_SET_BYTES];
    uint8_t timeout_bytes[POSIX_TIMESPEC_BYTES];
    uint8_t sigmask_bytes[POSIX_SIGSET_BYTES];
    posix_fd_set *rp = (void *)0, *wp = (void *)0, *ep = (void *)0;
    if (read_ptr) {
        if (!native_posix_read_guest(memory, read_ptr, rds_bytes,
                                     sizeof(rds_bytes)))
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_fd_set_decode(&rds, rds_bytes);
        rp = &rds;
    }
    if (write_ptr) {
        if (!native_posix_read_guest(memory, write_ptr, wrs_bytes,
                                     sizeof(wrs_bytes)))
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_fd_set_decode(&wrs, wrs_bytes);
        wp = &wrs;
    }
    if (except_ptr) {
        if (!native_posix_read_guest(memory, except_ptr, exs_bytes,
                                     sizeof(exs_bytes)))
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_fd_set_decode(&exs, exs_bytes);
        ep = &exs;
    }

    /* Decode timeout. */
    const posix_timespec *tsp = (void *)0;
    posix_timespec ts;
    if (timeout_ptr) {
        if (!native_posix_read_guest(memory, timeout_ptr, timeout_bytes,
                                     sizeof(timeout_bytes)))
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_timespec_decode(&ts, timeout_bytes);
        tsp = &ts;
    }

    /* Decode and validate the optional temporary signal mask. */
    posix_sigset mask;
    const posix_sigset *mask_ptr = (void *)0;
    if (sigmask_ptr) {
        if (!native_posix_read_guest(memory, sigmask_ptr, sigmask_bytes,
                                     sizeof(sigmask_bytes)))
            return native_posix_result(-POSIX_EINVAL, results, result_count);
        posix_sigset_decode(&mask, sigmask_bytes);
        mask_ptr = &mask;
    }

    int32_t ret = posix_kernel_pselect(store->kernel, nfds, rp, wp, ep,
                                       tsp, mask_ptr);

    /* A caught signal is delivered at the engine boundary, rather than via a
     * JavaScript callback.  The sigaction ABI stores a wasm function index in
     * its handler slot; invoke it with the POSIX signal number before the
     * interrupted pselect returns EINTR. */
    if (ret == -POSIX_EINTR) {
        int signal = posix_kernel_signal_last_delivered(store->kernel);
        posix_signal_disposition disposition;
        uint32_t handler = POSIX_SIG_DFL;
        if (signal > 0 &&
            posix_kernel_signal_get_disposition(store->kernel, signal,
                                                &disposition) == 0 &&
            disposition == POSIX_SIGNAL_HANDLER &&
            posix_kernel_signal_get_handler(store->kernel, signal, &handler) == 0 &&
            handler != POSIX_SIG_DFL && handler != POSIX_SIG_IGN) {
            wasm_value handler_arg;
            int handler_results = 0;
            exec_error handler_error;
            posix_sigset saved_mask;
            memset(&handler_error, 0, sizeof(handler_error));
            handler_arg.type = WASM_VALTYPE_I32;
            handler_arg.i32 = signal;
            if (posix_kernel_signal_enter_handler(store->kernel, signal,
                                                  &saved_mask) == 0) {
                (void)exec_invoke((waste_exec_engine *)caller, handler,
                                  &handler_arg, 1, NULL, &handler_results,
                                  &handler_error);
                posix_kernel_signal_leave_handler(store->kernel, &saved_mask);
            }
        }
    }

    if (ret == -POSIX_EAGAIN) {
        error->yield_reason = EXEC_YIELD_SELECT;
        return EXEC_YIELD;
    }

    /* Encode output sets back to guest memory on success. */
    if (ret >= 0) {
        if (rp) {
            posix_fd_set_encode(rds_bytes, rp);
            if (!native_posix_write_guest(memory, read_ptr, rds_bytes,
                                          sizeof(rds_bytes))) ret = -POSIX_EFAULT;
        }
        if (wp) {
            posix_fd_set_encode(wrs_bytes, wp);
            if (!native_posix_write_guest(memory, write_ptr, wrs_bytes,
                                          sizeof(wrs_bytes))) ret = -POSIX_EFAULT;
        }
        if (ep) {
            posix_fd_set_encode(exs_bytes, ep);
            if (!native_posix_write_guest(memory, except_ptr, exs_bytes,
                                          sizeof(exs_bytes))) ret = -POSIX_EFAULT;
        }
    }

    return native_posix_result(ret, results, result_count);
}

/* Return the active image's fixed-width startup block pointer.  This is the
 * only guest-facing way to discover argv/envp storage; the block itself stays
 * in the process image and never crosses the host boundary as a pointer. */
static exec_status native_posix_startup_v1(void *data, const wasm_value *args,
                                           int arg_count, wasm_value *results,
                                           int *result_count, exec_error *error,
                                           const waste_exec_engine *caller) {
    native_store *store = (native_store *)data;
    native_process_capsule *capsule;
    (void)caller; (void)error;
    if (!store || arg_count != 0)
        return native_posix_result(-POSIX_EINVAL, results, result_count);
    capsule = native_store_active_capsule(store);
    if (!capsule || !capsule->image || capsule->image->startup_ptr == 0)
        return native_posix_result(-POSIX_ENOENT, results, result_count);
    return native_posix_result((int32_t)capsule->image->startup_ptr, results,
                               result_count);
}

/* ---- POSIX function dispatch tables ---- */

static exec_host_func native_posix_function(const char *module,
                                             const char *name) {
    if (strcmp(module, "waste_kernel") == 0) {
        if (strcmp(name, "startup_v1") == 0) return native_posix_startup_v1;
        if (strcmp(name, "select_v1") == 0) return native_posix_select;
        if (strcmp(name, "pselect_v1") == 0) return native_posix_pselect;
        if (strcmp(name, POSIX_KERNEL_PATH_ACCESS_V1) == 0)
            return native_posix_path_access_v1;
        if (strcmp(name, POSIX_KERNEL_PATH_STAT_V1) == 0)
            return native_posix_path_stat_v1;
        if (strcmp(name, "isatty_v1") == 0) return native_posix_isatty;
        if (strcmp(name, "tcgetattr_v1") == 0) return native_posix_tcgetattr;
        if (strcmp(name, "tcsetattr_v1") == 0) return native_posix_tcsetattr;
        if (strcmp(name, "tcflow_v1") == 0) return native_posix_tcflow;
        if (strcmp(name, "ioctl_v1") == 0) return native_posix_ioctl;
        if (strcmp(name, "getcwd") == 0) return native_posix_getcwd;
        if (strcmp(name, "chdir") == 0) return native_posix_chdir;
        if (strcmp(name, "lseek") == 0) return native_posix_lseek;
        if (strcmp(name, "readlink_v1") == 0) return native_posix_readlink;
        if (strcmp(name, "fchdir_v1") == 0) return native_posix_fchdir;
        if (strcmp(name, "fcntl_v1") == 0) return native_posix_fcntl;
        if (strcmp(name, "pipe_v1") == 0) return native_posix_pipe_v1;
        return (void *)0;
    }
    if (strcmp(module, "env") != 0) return (void *)0;
    if (strcmp(name, "open") == 0) return native_posix_open;
    if (strcmp(name, "shm_open") == 0) return native_posix_shm_open;
    if (strcmp(name, "shm_unlink") == 0) return native_posix_shm_unlink;
    if (strcmp(name, "close") == 0) return native_posix_close;
    if (strcmp(name, "read") == 0) return native_posix_read;
    if (strcmp(name, "write") == 0) return native_posix_write;
    if (strcmp(name, "getcwd") == 0) return native_posix_getcwd;
    if (strcmp(name, "chdir") == 0) return native_posix_chdir;
    if (strcmp(name, "lseek") == 0) return native_posix_lseek;
    if (strcmp(name, "readdir_v1") == 0) return native_posix_readdir;
    if (strcmp(name, "getpgrp") == 0) return native_posix_getpgrp;
    if (strcmp(name, "tcgetpgrp") == 0) return native_posix_tcgetpgrp;
    if (strcmp(name, "isatty") == 0) return native_posix_isatty;
    if (strcmp(name, "tcflow") == 0) return native_posix_tcflow;
    if (strcmp(name, "time") == 0) return native_posix_i64_zero;
    if (strcmp(name, "exit") == 0 || strcmp(name, "_exit") == 0)
        return native_posix_exit;
    if (strcmp(name, "atexit") == 0) return native_posix_i32_zero;
    if (strcmp(name, "__fpurge") == 0) return native_posix_i32_zero;
    if (strcmp(name, "raise") == 0) return native_posix_raise;
    if (strcmp(name, "kill") == 0) return native_posix_kill;
    if (strcmp(name, "killpg") == 0) return native_posix_killpg;
    if (strcmp(name, "abort") == 0 || strcmp(name, "siglongjmp") == 0)
        return native_posix_void;
    if (strcmp(name, "sigsetjmp") == 0 || strcmp(name, "alarm") == 0 ||
        strcmp(name, "sigemptyset") == 0 || strcmp(name, "sigaddset") == 0 ||
        strcmp(name, "sigdelset") == 0 || strcmp(name, "sigismember") == 0 ||
        strcmp(name, "sigprocmask") == 0 ||
        strcmp(name, "setitimer") == 0 || strcmp(name, "sleep") == 0 ||
        strcmp(name, "gettimeofday") == 0 || strcmp(name, "getrusage") == 0 ||
        strcmp(name, "umask") == 0)
        return native_posix_i32_zero;
    if (strcmp(name, "dup") == 0) return native_posix_dup;
    if (strcmp(name, "dup2") == 0) return native_posix_dup2;
    if (strcmp(name, "fcntl") == 0) return native_posix_fcntl;
    if (strcmp(name, "setpgid") == 0) return native_posix_setpgid;
    if (strcmp(name, "tcsetpgrp") == 0) return native_posix_tcsetpgrp;
    if (strcmp(name, "tcgetattr") == 0) return native_posix_tcgetattr;
    if (strcmp(name, "tcsetattr") == 0) return native_posix_tcsetattr;
    if (strcmp(name, "ioctl") == 0) return native_posix_ioctl;
    /* Legacy applications such as the prebuilt Bash image import the libc
     * names directly.  Route them through the same engine-owned readiness
     * implementation as the versioned waste_kernel ABI. */
    if (strcmp(name, "select") == 0) return native_posix_select;
    if (strcmp(name, "pselect") == 0) return native_posix_pselect;
    if (strcmp(name, "sigaction") == 0) return native_posix_sigaction;
    if (strcmp(name, "getpid") == 0) return native_posix_getpid;
    if (strcmp(name, "getppid") == 0) return native_posix_getppid;
    if (strcmp(name, "access") == 0 || strcmp(name, "eaccess") == 0)
        return native_posix_access;
    if (strcmp(name, "faccessat") == 0) return native_posix_faccessat;
    if (strcmp(name, "stat") == 0) return native_posix_stat;
    if (strcmp(name, "lstat") == 0) return native_posix_lstat;
    if (strcmp(name, "fstat") == 0) return native_posix_fstat;
    if (strcmp(name, "mkdir") == 0) return native_posix_mkdir;
    if (strcmp(name, "unlink") == 0) return native_posix_unlink;
    if (strcmp(name, "rename") == 0) return native_posix_rename;
    if (strcmp(name, "rmdir") == 0) return native_posix_rmdir;
    if (strcmp(name, "readlink") == 0) return native_posix_readlink;
    if (strcmp(name, "lseek") == 0) return native_posix_lseek;
    if (strcmp(name, "ftruncate") == 0) return native_posix_ftruncate;
    if (strcmp(name, "mmap") == 0) return native_posix_mmap;
    if (strcmp(name, "munmap") == 0) return native_posix_munmap;
    if (strcmp(name, "msync") == 0) return native_posix_msync;
    if (strcmp(name, "mprotect") == 0) return native_posix_mprotect;
    if (strcmp(name, "fork") == 0) return native_posix_fork;
    if (strcmp(name, "pipe") == 0) return native_posix_pipe;
    if (strcmp(name, "getgroups") == 0 ||
        strcmp(name, "confstr") == 0 || strcmp(name, "fchmod") == 0 ||
        strcmp(name, "rmdir") == 0)
        return native_posix_i32_negative;
    if (strcmp(name, "waitpid") == 0) return native_posix_waitpid;
    if (strcmp(name, "execve") == 0) return native_posix_execve;
    if (strcmp(name, "getdtablesize") == 0)
        return native_posix_i32_sixty_four;
    return (void *)0;
}

static exec_host_control native_posix_control(const char *module,
                                              const char *name) {
    if (strcmp(module, "env") != 0) return EXEC_HOST_CONTROL_NONE;
    if (strcmp(name, "sigsetjmp") == 0)
        return EXEC_HOST_CONTROL_SIGSETJMP;
    if (strcmp(name, "siglongjmp") == 0)
        return EXEC_HOST_CONTROL_SIGLONGJMP;
    if (strcmp(name, "exit") == 0)
        return EXEC_HOST_CONTROL_EXIT;
    return EXEC_HOST_CONTROL_NONE;
}

/* ---- Host resolver wrapping POSIX stubs for the shared linker ---- */

int browser_host_resolver(const char *module, const char *name,
                           void *context, native_host_binding *out) {
    exec_host_func func = native_posix_function(module, name);
    if (!func) return 0;
    out->function = func;
    out->host_data = context;
    out->control = native_posix_control(module, name);
    /* Bash is built with 64-bit off_t while the current guest-libc module's
     * public off_t remains Wasm32 long.  Route Bash's env.lseek import to the
     * engine adapter with its native (i32, i64, i32) -> i64 signature. */
    out->prefer_over_module = strcmp(module, "env") == 0 &&
        (strcmp(name, "lseek") == 0 || strcmp(name, "__fpurge") == 0);
    return 1;
}
