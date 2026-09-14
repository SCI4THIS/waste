#include "wasm_module.h"

#include <stdlib.h>
#include <string.h>

void wasm_module_init(wasm_module *module) {
    if (module) memset(module, 0, sizeof(*module));
}

void wasm_module_dispose(wasm_module *module) {
    if (!module) return;
    free(module->imports);
    free(module->sections);
    free(module->owned_source);
    memset(module, 0, sizeof(*module));
}
