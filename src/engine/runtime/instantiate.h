#ifndef WASTE_RUNTIME_INSTANTIATE_H
#define WASTE_RUNTIME_INSTANTIATE_H

#include "runtime/engine_internal.h"

#include <stddef.h>
#include <stdint.h>

/* Instantiate mutable state from an owned, uninstantiated decoded module. */
exec_status wasm_instantiate_module(const wasm_module *module,
                                    const exec_imports *imports,
                                    waste_exec_engine **engine_out,
                                    exec_error *error);

/* Allocation operations used while the current decoder is being separated
 * from the instance representation. They centralize mutable module state in
 * the instantiation layer rather than the binary parser. */
exec_status wasm_instance_allocate_memory(exec_memory *memory,
                                          uint64_t initial,
                                          uint64_t maximum,
                                          uint32_t flags,
                                          exec_error *error);
exec_status wasm_instance_allocate_table(exec_table *table,
                                         waste_exec_engine *owner,
                                         wasm_valtype element_type,
                                         uint64_t initial,
                                         uint64_t maximum,
                                         uint32_t flags,
                                         exec_error *error);
exec_status wasm_instance_copy_segment(uint8_t **destination,
                                       const uint8_t *source,
                                       uint32_t length,
                                       exec_error *error);
exec_status wasm_instance_allocate_elements(exec_table_element **elements,
                                            uint32_t count,
                                            exec_error *error);
void wasm_instance_initialize_global(exec_global *global,
                                     waste_exec_engine *owner,
                                     wasm_valtype declared_type,
                                     uint8_t mutable_,
                                     const wasm_value *initial);
void wasm_instance_initialize_tag(exec_tag *tag,
                                  waste_exec_engine *owner,
                                  uint32_t type_index);
void *wasm_instance_resize_objects(void *objects, size_t old_count,
                                   size_t new_count, size_t item_size,
                                   exec_status failure_status,
                                   const char *message,
                                   exec_error *error);
void *wasm_instance_allocate_objects(size_t count, size_t item_size,
                                     exec_status failure_status,
                                     const char *message,
                                     exec_error *error);

#endif
