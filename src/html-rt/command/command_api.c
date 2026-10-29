/* Explicit host ABI over the public C session API. All memory arguments refer
 * to host allocations registered here; no guest pointer is accepted. */
#include "../../engine/include/waste.h"
#include "../../engine/wasm/decode.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define EXPORT(name) __attribute__((export_name("waste_command_" name)))
#define SESSION_COUNT 16u
#define ALLOCATION_COUNT 128u
#define OPTIONS_BYTES 64u
#define RESULT_BYTES 328u
#define INFO_BYTES 32u

typedef struct { waste_session *session; uint32_t generation; } session_slot;
typedef struct { uint8_t *bytes; uint32_t size; } allocation;
static session_slot sessions[SESSION_COUNT];
static allocation allocations[ALLOCATION_COUNT];
static waste_error last_error;

__attribute__((import_module("waste_command_host"), import_name("monotonic_ns")))
extern uint64_t host_monotonic(void);
__attribute__((import_module("waste_command_host"), import_name("realtime_ns")))
extern uint64_t host_realtime(void);
__attribute__((import_module("waste_command_host"), import_name("random")))
extern int32_t host_random(uint32_t, uint32_t);

static uint64_t monotonic(void *data) { (void)data; return host_monotonic(); }
static uint64_t realtime(void *data) { (void)data; return host_realtime(); }
static int32_t random_bytes(void *data, void *bytes, uint32_t length) {
    (void)data;
    return host_random((uint32_t)(uintptr_t)bytes, length);
}
static int32_t fail(waste_status status, const char *message) {
    last_error.status = status;
    snprintf(last_error.message, sizeof(last_error.message), "%s", message);
    return -(int32_t)status;
}
static int32_t result(waste_status status) { return -(int32_t)status; }

static int span(uint32_t pointer, uint32_t length) {
    if (!length && !pointer) return 1;
    for (uint32_t i = 0; i < ALLOCATION_COUNT; i++) {
        uint32_t base = (uint32_t)(uintptr_t)allocations[i].bytes;
        if (base && pointer >= base && pointer - base <= allocations[i].size &&
            length <= allocations[i].size - (pointer - base)) return 1;
    }
    return 0;
}
static uint32_t read32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
        (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}
static void write32(uint8_t *bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; i++) bytes[i] = (uint8_t)(value >> (i * 8));
}
static void write64(uint8_t *bytes, uint64_t value) {
    write32(bytes, (uint32_t)value); write32(bytes + 4, (uint32_t)(value >> 32));
}
static waste_session *lookup(uint32_t handle) {
    uint32_t index = (handle & 0xffffu);
    if (!index || index > SESSION_COUNT || !sessions[index - 1].session ||
        sessions[index - 1].generation != handle >> 16) {
        fail(WASTE_ERROR_NOT_FOUND, "invalid or stale session handle");
        return NULL;
    }
    return sessions[index - 1].session;
}
static int string_copy(uint32_t pointer, uint32_t size, char *out, uint32_t capacity) {
    if (size >= capacity || !span(pointer, size)) return 0;
    const uint8_t *bytes = (const uint8_t *)(uintptr_t)pointer;
    if (!valid_utf8(bytes, size) || (size && memchr(bytes, 0, size))) return 0;
    if (size) memcpy(out, bytes, size);
    out[size] = 0;
    return 1;
}

EXPORT("version") uint32_t command_version(void) { return 0x00010000u; }
EXPORT("features") uint32_t command_features(void) { return WASTE_SESSION_FEATURES; }
EXPORT("options_size") uint32_t command_options_size(void) { return OPTIONS_BYTES; }
EXPORT("result_size") uint32_t command_result_size(void) { return RESULT_BYTES; }
EXPORT("file_info_size") uint32_t command_info_size(void) { return INFO_BYTES; }
EXPORT("alloc") uint32_t command_alloc(uint32_t size) {
    if (!size || size > 128u * 1024 * 1024) { fail(WASTE_ERROR_MALFORMED, "invalid allocation size"); return 0; }
    for (uint32_t i = 0; i < ALLOCATION_COUNT; i++) if (!allocations[i].bytes) {
        allocations[i].bytes = malloc(size);
        if (!allocations[i].bytes) break;
        allocations[i].size = size;
        return (uint32_t)(uintptr_t)allocations[i].bytes;
    }
    fail(WASTE_ERROR_RESOURCE, "host transfer allocation failed");
    return 0;
}
EXPORT("free") int32_t command_free(uint32_t pointer) {
    for (uint32_t i = 0; i < ALLOCATION_COUNT; i++) {
        if (pointer && (uint32_t)(uintptr_t)allocations[i].bytes == pointer) {
            free(allocations[i].bytes);
            memset(&allocations[i], 0, sizeof(allocations[i]));
            return 0;
        }
    }
    return fail(WASTE_ERROR_NOT_FOUND, "invalid transfer allocation");
}
EXPORT("error") int32_t command_error(uint32_t pointer, uint32_t capacity) {
    if (!span(pointer, capacity) || capacity < sizeof(last_error.message))
        return -(int32_t)WASTE_ERROR_MALFORMED;
    memcpy((void *)(uintptr_t)pointer, last_error.message, sizeof(last_error.message));
    return (int32_t)last_error.status;
}
EXPORT("create") uint32_t command_create(uint32_t major, uint32_t minor,
    uint32_t features, uint32_t pointer, uint32_t length) {
    if (length != OPTIONS_BYTES || !span(pointer, length)) {
        fail(WASTE_ERROR_MALFORMED, "invalid options record"); return 0;
    }
    const uint8_t *bytes = (const uint8_t *)(uintptr_t)pointer;
    if (read32(bytes) != OPTIONS_BYTES) { fail(WASTE_ERROR_MALFORMED, "invalid options size"); return 0; }
    for (uint32_t at = 40; at < OPTIONS_BYTES; at += 4) if (read32(bytes + at)) {
        fail(WASTE_ERROR_UNSUPPORTED, "unsupported options field"); return 0;
    }
    waste_session_options options;
    waste_session_options_init(&options);
    options.abi_major = major; options.abi_minor = minor; options.required_features = features;
    options.max_input_bytes = read32(bytes + 4); options.max_output_bytes = read32(bytes + 8);
    options.timeout_ms = read32(bytes + 12); options.quantum_ms = read32(bytes + 16);
    if (read32(bytes + 20)) options.max_memory_pages = read32(bytes + 20);
    if (read32(bytes + 24)) options.max_processes = read32(bytes + 24);
    if (read32(bytes + 28)) options.max_filesystem_bytes = read32(bytes + 28);
    options.max_instructions = (uint64_t)read32(bytes + 36) << 32 | read32(bytes + 32);
    options.monotonic_ns = monotonic; options.realtime_ns = realtime; options.random_bytes = random_bytes;
    for (uint32_t i = 0; i < SESSION_COUNT; i++) {
        if (sessions[i].session || sessions[i].generation >= 0xffffu) continue;
        if (waste_session_create(&options, &sessions[i].session, &last_error) != WASTE_OK) return 0;
        sessions[i].generation++;
        return sessions[i].generation << 16 | (i + 1);
    }
    fail(WASTE_ERROR_UNSUPPORTED, "session handle capacity exceeded"); return 0;
}
EXPORT("destroy") int32_t command_destroy(uint32_t handle) {
    if (!lookup(handle)) return -(int32_t)WASTE_ERROR_NOT_FOUND;
    waste_session_delete(&sessions[(handle & 0xffffu) - 1].session);
    return 0;
}
EXPORT("put_file") int32_t command_put_file(uint32_t handle, uint32_t path,
    uint32_t path_size, uint32_t data, uint32_t size, uint32_t mode) {
    waste_session *session = lookup(handle);
    char name[256];
    if (!session) return -(int32_t)WASTE_ERROR_NOT_FOUND;
    if (!string_copy(path, path_size, name, sizeof(name)) || !span(data, size))
        return fail(WASTE_ERROR_MALFORMED, "invalid file buffers");
    return result(waste_session_put_file(session, name, (const uint8_t *)(uintptr_t)data, size, mode, &last_error));
}
EXPORT("mkdir") int32_t command_mkdir(uint32_t handle, uint32_t path, uint32_t length, uint32_t mode) {
    waste_session *session = lookup(handle); char name[256];
    if (!session) return -(int32_t)WASTE_ERROR_NOT_FOUND;
    if (!string_copy(path, length, name, sizeof(name))) return fail(WASTE_ERROR_MALFORMED, "invalid directory path");
    return result(waste_session_mkdir(session, name, mode, &last_error));
}
EXPORT("cwd") int32_t command_cwd(uint32_t handle, uint32_t path, uint32_t length) {
    waste_session *session = lookup(handle); char name[256];
    if (!session) return -(int32_t)WASTE_ERROR_NOT_FOUND;
    if (!string_copy(path, length, name, sizeof(name))) return fail(WASTE_ERROR_MALFORMED, "invalid cwd path");
    return result(waste_session_set_cwd(session, name, &last_error));
}
EXPORT("input") int32_t command_input(uint32_t handle, uint32_t pointer, uint32_t length, uint32_t eof) {
    waste_session *session = lookup(handle);
    if (!session) return -(int32_t)WASTE_ERROR_NOT_FOUND;
    if (!span(pointer, length)) return fail(WASTE_ERROR_MALFORMED, "invalid input buffer");
    return result(waste_session_input(session, (const uint8_t *)(uintptr_t)pointer, length, eof, &last_error));
}
EXPORT("start") int32_t command_start(uint32_t handle, uint32_t pointer, uint32_t length) {
    waste_session *session = lookup(handle);
    if (!session) return -(int32_t)WASTE_ERROR_NOT_FOUND;
    if (!span(pointer, length) || length < 12 || length > 128 * 1024)
        return fail(WASTE_ERROR_MALFORMED, "invalid command vector record");
    const uint8_t *bytes = (const uint8_t *)(uintptr_t)pointer;
    uint32_t argc = read32(bytes), envc = read32(bytes + 4), path_size = read32(bytes + 8);
    int default_environment = envc == UINT32_MAX;
    if (default_environment) envc = 0;
    if (argc > 126 || envc > 255 || path_size >= 256 || path_size > length - 12)
        return fail(WASTE_ERROR_MALFORMED, "invalid command vector counts");
    char path[256];
    if (!string_copy(pointer + 12, path_size, path, sizeof(path)))
        return fail(WASTE_ERROR_MALFORMED, "invalid executable path");
    char *copy = malloc(length + argc + envc);
    if (!copy) return fail(WASTE_ERROR_RESOURCE, "cannot allocate command vectors");
    const char *args[127], *env[256];
    uint32_t cursor = 12 + path_size, at = 0;
    for (uint32_t i = 0; i < argc + envc; i++) {
        if (cursor > length || length - cursor < 4) goto invalid;
        uint32_t size = read32(bytes + cursor); cursor += 4;
        if (size > length - cursor || !valid_utf8(bytes + cursor, size) ||
            (size && memchr(bytes + cursor, 0, size))) goto invalid;
        if (i < argc) args[i] = copy + at; else env[i - argc] = copy + at;
        memcpy(copy + at, bytes + cursor, size); copy[at + size] = 0;
        at += size + 1; cursor += size;
    }
    if (cursor != length) goto invalid;
    waste_status status = waste_session_start(session, path, args, argc,
        default_environment ? NULL : env, envc, &last_error);
    free(copy); return result(status);
invalid:
    free(copy); return fail(WASTE_ERROR_MALFORMED, "malformed command vector");
}
EXPORT("step") int32_t command_step(uint32_t handle) {
    waste_session *session = lookup(handle);
    return session ? result(waste_session_step(session, &last_error)) : -(int32_t)WASTE_ERROR_NOT_FOUND;
}
EXPORT("cancel") int32_t command_cancel(uint32_t handle) {
    waste_session *session = lookup(handle);
    if (!session) return -(int32_t)WASTE_ERROR_NOT_FOUND;
    waste_session_cancel(session); return 0;
}
EXPORT("inspect") int32_t command_inspect(uint32_t handle, uint32_t pointer, uint32_t length) {
    waste_session *session = lookup(handle);
    if (!session) return -(int32_t)WASTE_ERROR_NOT_FOUND;
    if (length != RESULT_BYTES || !span(pointer, length)) return fail(WASTE_ERROR_MALFORMED, "invalid result record");
    waste_session_result value;
    waste_status status = waste_session_inspect(session, &value);
    if (status != WASTE_OK) return result(status);
    uint8_t *bytes = (uint8_t *)(uintptr_t)pointer;
    memset(bytes, 0, length);
    write32(bytes, RESULT_BYTES); write32(bytes + 4, value.state); write32(bytes + 8, value.wait);
    write32(bytes + 12, (uint32_t)value.exit_code); write32(bytes + 16, value.signal);
    write32(bytes + 20, value.stdout_bytes); write32(bytes + 24, value.stderr_bytes);
    write64(bytes + 32, value.elapsed_ns); write64(bytes + 40, value.wait_ns);
    memcpy(bytes + 48, value.message, 256);
    write32(bytes + 304, value.memory_pages_peak);
    write32(bytes + 308, value.filesystem_bytes_peak);
    write32(bytes + 312, value.processes_peak);
    write64(bytes + 320, value.instructions);
    return 0;
}
EXPORT("output") int32_t command_output(uint32_t handle, uint32_t fd, uint32_t offset,
    uint32_t pointer, uint32_t capacity) {
    waste_session *session = lookup(handle); uint32_t copied = 0;
    if (!session) return -(int32_t)WASTE_ERROR_NOT_FOUND;
    if (!span(pointer, capacity)) return fail(WASTE_ERROR_MALFORMED, "invalid output buffer");
    waste_status status = waste_session_copy_output(session, fd, offset,
        (uint8_t *)(uintptr_t)pointer, capacity, &copied, &last_error);
    return status == WASTE_OK ? (int32_t)copied : result(status);
}
static void encode_info(uint8_t *bytes, const waste_session_file_info *info) {
    write32(bytes, info->kind); write32(bytes + 4, info->mode); write64(bytes + 8, info->size);
    write64(bytes + 16, (uint64_t)info->mtime_sec); write64(bytes + 24, (uint64_t)info->mtime_nsec);
}
EXPORT("stat") int32_t command_stat(uint32_t handle, uint32_t path, uint32_t length, uint32_t output) {
    waste_session *session = lookup(handle); char name[256]; waste_session_file_info info;
    if (!session) return -(int32_t)WASTE_ERROR_NOT_FOUND;
    if (!string_copy(path, length, name, sizeof(name)) || !span(output, INFO_BYTES))
        return fail(WASTE_ERROR_MALFORMED, "invalid stat buffers");
    waste_status status = waste_session_stat(session, name, &info, &last_error);
    if (status == WASTE_OK) encode_info((uint8_t *)(uintptr_t)output, &info);
    return result(status);
}
EXPORT("read_file") int32_t command_read_file(uint32_t handle, uint32_t path, uint32_t length,
    uint32_t offset_low, uint32_t offset_high, uint32_t output, uint32_t capacity) {
    waste_session *session = lookup(handle); char name[256]; uint32_t copied = 0;
    if (!session) return -(int32_t)WASTE_ERROR_NOT_FOUND;
    if (!string_copy(path, length, name, sizeof(name)) || !span(output, capacity))
        return fail(WASTE_ERROR_MALFORMED, "invalid file read buffers");
    waste_status status = waste_session_read_file(session, name,
        (uint64_t)offset_high << 32 | offset_low, (uint8_t *)(uintptr_t)output, capacity, &copied, &last_error);
    return status == WASTE_OK ? (int32_t)copied : result(status);
}
EXPORT("list") int32_t command_list(uint32_t handle, uint32_t path, uint32_t length,
    uint32_t index, uint32_t output, uint32_t capacity, uint32_t metadata) {
    waste_session *session = lookup(handle); char name[256]; waste_session_file_info info;
    if (!session) return -(int32_t)WASTE_ERROR_NOT_FOUND;
    if (!string_copy(path, length, name, sizeof(name)) || !span(output, capacity) || !span(metadata, INFO_BYTES))
        return fail(WASTE_ERROR_MALFORMED, "invalid directory buffers");
    waste_status status = waste_session_list(session, name, index, (char *)(uintptr_t)output, capacity, &info, &last_error);
    if (status == WASTE_OK) encode_info((uint8_t *)(uintptr_t)metadata, &info);
    return result(status);
}
