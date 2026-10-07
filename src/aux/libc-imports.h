#ifndef WASTE_AUX_LIBC_IMPORTS_H
#define WASTE_AUX_LIBC_IMPORTS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/* Keep the SDK's function types; attach the shared-library namespace when
   compiling these clients instead of rewriting the linked Wasm binary. */
#define WASTE_LIBC_IMPORT(name) \
    extern __typeof__(name) name __attribute__((import_module("libc")))

WASTE_LIBC_IMPORT(printf);
WASTE_LIBC_IMPORT(fprintf);
WASTE_LIBC_IMPORT(strcmp);
WASTE_LIBC_IMPORT(strlen);
WASTE_LIBC_IMPORT(strrchr);
WASTE_LIBC_IMPORT(malloc);
WASTE_LIBC_IMPORT(free);
WASTE_LIBC_IMPORT(open);
WASTE_LIBC_IMPORT(close);

/* read and fstat are supplied by the runtime, not the installed libc. */

#undef WASTE_LIBC_IMPORT

#endif
