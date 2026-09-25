#ifndef WASTE_SYS_TYPES_H
#define WASTE_SYS_TYPES_H

/* Fixed-width Wasm32 application ABI types.  These declarations are kept
   independent of the host kernel's ABI and match the structures in helper.h. */
typedef signed long ssize_t;
#ifndef SSIZE_MAX
#define SSIZE_MAX 0x7fffffffL
#endif
#ifndef size_t
typedef unsigned long size_t;
#endif
typedef signed long off_t;
typedef signed long off64_t;
typedef signed long blksize_t;
typedef signed long blkcnt_t;
typedef unsigned long ino_t;
typedef unsigned long dev_t;
typedef unsigned long nlink_t;
#ifndef mode_t
typedef unsigned int mode_t;
#endif
#ifndef uid_t
typedef unsigned int uid_t;
#endif
#ifndef gid_t
typedef unsigned int gid_t;
#endif
#ifndef pid_t
typedef int pid_t;
#endif
typedef long time_t;
#ifndef socklen_t
typedef unsigned int socklen_t;
#endif

#endif
