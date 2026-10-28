#ifndef WASTE_M4_COMPAT_H
#define WASTE_M4_COMPAT_H
#include "../../config.h"
/* Match the shared kernel pathname budget in this private build profile. */
#ifndef PATH_MAX
#define PATH_MAX VFS_PATH_MAX_BYTES
#endif
#include "../coreutils/include/waste-gnulib-compat.h"
/* The mounted SDK's named Gnulib profile already supplies this helper. */
#define GNULIB_defined_mbszero 1
/* This Gnulib revision uses the public spelling of the category mapping. */
#define gl_log2_lc_mask(category) (category)
#define gl_log2_lcmask_to_index(category) (category)
#define gl_index_to_log2_lcmask(index) (index)
#endif
