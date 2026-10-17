#ifndef WASTE_CLANG_LIMITS_H
#define WASTE_CLANG_LIMITS_H
#include_next <limits.h>
#include "../../../config.h"

/* LLVM's private absolute-path buffers follow the actual guest namespace
 * bound. Do not export native limits or duplicate the configuration value. */
#define PATH_MAX VFS_PATH_MAX_BYTES
#endif
