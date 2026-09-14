#include "include/waste_engine.h"

#include "runtime/engine_internal.h"
#include "runtime/instantiate.h"
#include "wasm/wasm_decode.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct waste_module {
    wasm_module decoded;
};

struct waste_instance {
    waste_exec_engine *engine;
};

static waste_status public_status(exec_status status) {
    switch (status) {
        case EXEC_OK: return WASTE_OK;
        case EXEC_ERROR_FORMAT: return WASTE_ERROR_MALFORMED;
        case EXEC_ERROR_UNSUPPORTED: return WASTE_ERROR_UNSUPPORTED;
        case EXEC_ERROR_EXCEPTION: return WASTE_ERROR_EXCEPTION;
        case EXEC_ERROR_NOT_FOUND: return WASTE_ERROR_NOT_FOUND;
        case EXEC_ERROR_EXIT: return WASTE_ERROR_EXIT;
        case EXEC_YIELD: return WASTE_YIELD;
        case EXEC_ERROR_LONGJMP:
        case EXEC_ERROR_TRAP: return WASTE_ERROR_TRAP;
    }
    return WASTE_ERROR_TRAP;
}

static void set_public_error(waste_error *destination, waste_status status,
                             size_t offset, const char *message) {
    if (!destination) return;
    destination->status = status;
    destination->offset = offset;
    if (!message) message = "";
    size_t length = strlen(message);
    if (length >= sizeof(destination->message))
        length = sizeof(destination->message) - 1;
    memcpy(destination->message, message, length);
    destination->message[length] = '\0';
}

static waste_status translate_exec_error(exec_status status,
                                         const exec_error *source,
                                         waste_error *destination) {
    waste_status translated = public_status(status);
    set_public_error(destination, translated, 0,
                     source ? source->message : "");
    return translated;
}

waste_status waste_module_decode(const uint8_t *bytes, size_t size,
                                 waste_module **module_out,
                                 waste_error *error) {
    wasm_decode_error decode_error;
    waste_module *module;
    wasm_decode_status status;

    if (!module_out || !bytes) {
        set_public_error(error, WASTE_ERROR_MALFORMED, 0,
                         "invalid module input");
        return WASTE_ERROR_MALFORMED;
    }
    *module_out = NULL;
    module = calloc(1, sizeof(*module));
    if (!module) {
        set_public_error(error, WASTE_ERROR_MALFORMED, 0,
                         "module allocation failed");
        return WASTE_ERROR_MALFORMED;
    }
    status = wasm_decode_module(bytes, size, &module->decoded, &decode_error);
    if (status != WASM_DECODE_OK) {
        set_public_error(error, WASTE_ERROR_MALFORMED, decode_error.offset,
                         decode_error.message);
        free(module);
        return WASTE_ERROR_MALFORMED;
    }
    *module_out = module;
    set_public_error(error, WASTE_OK, 0, "");
    return WASTE_OK;
}

void waste_module_delete(waste_module *module) {
    if (!module) return;
    wasm_module_dispose(&module->decoded);
    free(module);
}

waste_status waste_instance_create(const waste_module *module,
                                   waste_instance **instance_out,
                                   waste_error *error) {
    waste_instance *instance;
    exec_error internal_error;
    exec_status status;

    if (!module || !instance_out) {
        set_public_error(error, WASTE_ERROR_MALFORMED, 0,
                         "invalid instance input");
        return WASTE_ERROR_MALFORMED;
    }
    *instance_out = NULL;
    instance = calloc(1, sizeof(*instance));
    if (!instance) {
        set_public_error(error, WASTE_ERROR_MALFORMED, 0,
                         "instance allocation failed");
        return WASTE_ERROR_MALFORMED;
    }
    memset(&internal_error, 0, sizeof(internal_error));
    status = wasm_instantiate_module(&module->decoded, NULL,
                                     &instance->engine, &internal_error);
    if (status != EXEC_OK) {
        exec_free(instance->engine);
        free(instance);
        return translate_exec_error(status, &internal_error, error);
    }
    *instance_out = instance;
    set_public_error(error, WASTE_OK, 0, "");
    return WASTE_OK;
}

waste_status waste_instance_load(const uint8_t *bytes, size_t size,
                                 waste_instance **instance_out,
                                 waste_error *error) {
    waste_module *module = NULL;
    waste_status status = waste_module_decode(bytes, size, &module, error);
    if (status != WASTE_OK) return status;
    status = waste_instance_create(module, instance_out, error);
    waste_module_delete(module);
    return status;
}

void waste_instance_delete(waste_instance *instance) {
    if (!instance) return;
    exec_free(instance->engine);
    free(instance);
}

waste_status waste_instance_find_function(const waste_instance *instance,
                                          const char *name,
                                          uint32_t *function_out,
                                          waste_error *error) {
    exec_error internal_error;
    exec_status status;
    if (!instance || !name || !function_out) {
        set_public_error(error, WASTE_ERROR_MALFORMED, 0,
                         "invalid function lookup");
        return WASTE_ERROR_MALFORMED;
    }
    memset(&internal_error, 0, sizeof(internal_error));
    status = exec_find_export(instance->engine, name, function_out,
                              &internal_error);
    return translate_exec_error(status, &internal_error, error);
}

waste_status waste_instance_invoke(waste_instance *instance,
                                   uint32_t function,
                                   const waste_value *arguments,
                                   size_t argument_count,
                                   waste_value *results,
                                   size_t result_capacity,
                                   size_t *result_count,
                                   waste_error *error) {
    wasm_value internal_results[WAST_MAX_RESULTS];
    exec_error internal_error;
    int internal_count = 0;
    exec_status status;

    if (!instance || argument_count > INT_MAX ||
        (argument_count && !arguments) || !result_count) {
        set_public_error(error, WASTE_ERROR_MALFORMED, 0,
                         "invalid invocation input");
        return WASTE_ERROR_MALFORMED;
    }
    memset(&internal_error, 0, sizeof(internal_error));
    status = exec_invoke(instance->engine, function, arguments,
                         (int)argument_count, internal_results,
                         &internal_count, &internal_error);
    if (status != EXEC_OK)
        return translate_exec_error(status, &internal_error, error);
    if (internal_count < 0 || (size_t)internal_count > result_capacity ||
        (internal_count && !results)) {
        set_public_error(error, WASTE_ERROR_MALFORMED, 0,
                         "result buffer is too small");
        return WASTE_ERROR_MALFORMED;
    }
    if (internal_count)
        memcpy(results, internal_results,
               (size_t)internal_count * sizeof(*results));
    *result_count = (size_t)internal_count;
    set_public_error(error, WASTE_OK, 0, "");
    return WASTE_OK;
}
