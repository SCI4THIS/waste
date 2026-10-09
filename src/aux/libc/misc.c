/* misc.c — ABI size probes consumed by the select-abi test fixture. */

#include "include/helper.h"

i32 toascii(i32 character) { return character & 0x7f; }

u32 waste_fd_set_size(void)  { return sizeof(waste_fd_set); }
u32 waste_timeval_size(void) { return sizeof(waste_timeval); }
u32 waste_timespec_size(void){ return sizeof(waste_timespec); }
u32 waste_sigset_size(void)  { return sizeof(waste_sigset_t); }
i32 waste_fd_setsize(void)   { return WASTE_FD_SETSIZE; }
i32 waste_nfdbits(void)      { return WASTE_NFDBITS; }
u32 waste_stat_size(void)    { return sizeof(waste_stat); }
u32 waste_stat_mode_offset(void) { return offsetof(waste_stat, st_mode); }
u32 waste_stat_size_offset(void) { return offsetof(waste_stat, st_size); }
u32 waste_stat_atime_offset(void) { return offsetof(waste_stat, st_atime_sec); }
u32 waste_stat_ctime_nsec_offset(void) { return offsetof(waste_stat, st_ctime_nsec); }
