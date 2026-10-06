#ifndef WASTE_SYS_MMAN_H
#define WASTE_SYS_MMAN_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Current interpreter adapter uses an i64 offset, independently of the
 * legacy 32-bit off_t used by libc lseek/fseeko. No host mmap ABI is reused. */
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FIXED 0x10 /* not supported; use MAP_FIXED_NOREPLACE */
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FIXED_NOREPLACE 0x100000
#define MAP_FAILED ((void *)-1)
#define MS_ASYNC 1
#define MS_INVALIDATE 2
#define MS_SYNC 4
void *mmap(void *, size_t, int, int, int, int64_t);
int munmap(void *, size_t);
int mprotect(void *, size_t, int);
int msync(void *, size_t, int);
int shm_open(const char *, int, mode_t);
int shm_unlink(const char *);
#endif
