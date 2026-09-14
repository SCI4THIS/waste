#ifndef WASTE_ENGINE_H
#define WASTE_ENGINE_H

#include "waste_error.h"
#include "waste_value.h"

#include <stddef.h>
#include <stdint.h>

typedef struct waste_module waste_module;
typedef struct waste_instance waste_instance;

waste_status waste_module_decode(const uint8_t *bytes, size_t size,
                                 waste_module **module_out,
                                 waste_error *error);
void waste_module_delete(waste_module *module);

waste_status waste_instance_create(const waste_module *module,
                                   waste_instance **instance_out,
                                   waste_error *error);
waste_status waste_instance_load(const uint8_t *bytes, size_t size,
                                 waste_instance **instance_out,
                                 waste_error *error);
void waste_instance_delete(waste_instance *instance);

waste_status waste_instance_find_function(const waste_instance *instance,
                                          const char *name,
                                          uint32_t *function_out,
                                          waste_error *error);
waste_status waste_instance_invoke(waste_instance *instance,
                                   uint32_t function,
                                   const waste_value *arguments,
                                   size_t argument_count,
                                   waste_value *results,
                                   size_t result_capacity,
                                   size_t *result_count,
                                   waste_error *error);

#endif /* WASTE_ENGINE_H */
