#ifndef WASTE_SYS_SELECT_H
#define WASTE_SYS_SELECT_H

#include <sys/time.h>
#include <time.h>

#define FD_SETSIZE 1024
#define NFDBITS 32
typedef struct { unsigned long fds_bits[FD_SETSIZE / NFDBITS]; } fd_set;
#ifndef WASTE_SIGSET_T_DEFINED
# define WASTE_SIGSET_T_DEFINED 1
typedef struct { unsigned long __bits[4]; } sigset_t;
#endif

#define FD_ZERO(set) do { for (unsigned int __i = 0; __i < FD_SETSIZE / NFDBITS; ++__i) (set)->fds_bits[__i] = 0; } while (0)
#define FD_SET(fd, set) ((set)->fds_bits[(unsigned)(fd) / NFDBITS] |= 1UL << ((unsigned)(fd) % NFDBITS))
#define FD_CLR(fd, set) ((set)->fds_bits[(unsigned)(fd) / NFDBITS] &= ~(1UL << ((unsigned)(fd) % NFDBITS)))
#define FD_ISSET(fd, set) (((set)->fds_bits[(unsigned)(fd) / NFDBITS] & (1UL << ((unsigned)(fd) % NFDBITS))) != 0)

int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
int pselect(int, fd_set *, fd_set *, fd_set *, const struct timespec *, const sigset_t *);

#endif
