#ifndef WASTE_ABI_FCNTL_H
#define WASTE_ABI_FCNTL_H

/* WASTE's wasm32 record-lock boundary, independent of the host struct flock. */
#define WASTE_F_DUPFD 0
#define WASTE_F_GETFD 1
#define WASTE_F_SETFD 2
#define WASTE_F_GETFL 3
#define WASTE_F_SETFL 4
#define WASTE_F_DUPFD_CLOEXEC 1030
#define WASTE_F_GETLK 5
#define WASTE_F_SETLK 6
#define WASTE_F_SETLKW 7
#define WASTE_F_RDLCK 0
#define WASTE_F_WRLCK 1
#define WASTE_F_UNLCK 2
#define WASTE_FLOCK_BYTES 16
#define WASTE_FLOCK_TYPE_OFFSET 0
#define WASTE_FLOCK_WHENCE_OFFSET 2
#define WASTE_FLOCK_START_OFFSET 4
#define WASTE_FLOCK_LEN_OFFSET 8
#define WASTE_FLOCK_PID_OFFSET 12

#endif
