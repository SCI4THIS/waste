/*
 * wast_general.h — General-purpose WAT/WAST interpreter for bulk-operations
 * and related Wasm features (memory.init, memory.copy, memory.fill,
 * table.init, table.copy, table.fill, call_indirect, ref.func, etc.)
 *
 * Self-contained: parses WAT text, encodes to binary, executes, runs WAST script.
 */
#ifndef WAST_GENERAL_H
#define WAST_GENERAL_H

#ifndef WASTE_FREESTANDING
#include <stddef.h>
#include <stdint.h>
#endif

/* ---- Opaque instance ---- */
typedef struct gen_instance gen_instance;

/* ---- Script runner ---- */

/*
 * Parse and run a WAST file using the general interpreter.
 * Outputs JSON to stdout matching:
 *   {"file":"...","assertions":[...],"passed":N,"total":N}
 * Returns 0 if all assertions pass, 1 otherwise.
 */
int wast_general_run(const char *path);

/* ---- Browser API ---- */

/*
 * Load a WAT text module.  Returns 0 on success.
 */
uint32_t waste_gen_load_module(uint32_t text_ptr, uint32_t text_len);

/*
 * Invoke an exported function (fire-and-forget, no result check).
 * args_ptr: flat-value array (33 bytes each), arg_count values.
 * Returns 0 on success.
 */
uint32_t waste_gen_invoke(uint32_t name_ptr, uint32_t name_len,
                          uint32_t args_ptr, uint32_t arg_count);

/*
 * Assert-return: invoke and compare results.
 * alts_ptr: alt_count * result_count flat values.
 * Returns 1 on pass, 0 on fail.
 */
uint32_t waste_gen_assert_return(uint32_t name_ptr, uint32_t name_len,
                                 uint32_t args_ptr, uint32_t arg_count,
                                 uint32_t alts_ptr, uint32_t alt_count,
                                 uint32_t result_count);

/* Pointer to null-terminated error message string. */
uint32_t waste_gen_error_ptr(void);

#endif /* WAST_GENERAL_H */
