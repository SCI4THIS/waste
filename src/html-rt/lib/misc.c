/* misc.c — ABI size probes consumed by the select-abi test fixture. */

#include "include/helper.h"

u32 waste_fd_set_size(void)  { return sizeof(waste_fd_set); }
u32 waste_timeval_size(void) { return sizeof(waste_timeval); }
u32 waste_timespec_size(void){ return sizeof(waste_timespec); }
u32 waste_sigset_size(void)  { return sizeof(waste_sigset_t); }
i32 waste_fd_setsize(void)   { return WASTE_FD_SETSIZE; }
i32 waste_nfdbits(void)      { return WASTE_NFDBITS; }
