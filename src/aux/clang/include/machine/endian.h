#ifndef WASTE_CLANG_MACHINE_ENDIAN_H
#define WASTE_CLANG_MACHINE_ENDIAN_H

/* LLVM's bare-target fallback uses the BSD header spelling. Keep this
 * compatibility view private and derive its values from the guest SDK. */
#include <endian.h>

#define LITTLE_ENDIAN __LITTLE_ENDIAN
#define BIG_ENDIAN __BIG_ENDIAN
#define BYTE_ORDER __BYTE_ORDER

#endif
