#ifndef WASTE_BASH_STDLIB_H
#define WASTE_BASH_STDLIB_H
#define abort waste_bash_unavailable_abort
#include_next <stdlib.h>
#undef abort
/* Fatal internal errors terminate the guest through a Wasm trap. */
#define abort() __builtin_trap()
#endif
