#include <stdlib.h>

__attribute__((import_module("waste_kernel"), import_name("abort_v1")))
extern void waste_kernel_abort_v1(void);

_Noreturn void abort(void) {
    waste_kernel_abort_v1();
    __builtin_trap();
}
