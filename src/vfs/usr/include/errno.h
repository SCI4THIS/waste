#ifndef WASTE_ERRNO_H
#define WASTE_ERRNO_H

int *__errno_location(void);
#define errno (*__errno_location())

#define EPERM 1
#define ENOENT 2
#define EINTR 4
#define EIO 5
#define ECHILD 10
#define EBADF 9
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EEXIST 17
#define EBUSY 16
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENOTTY 25
#define ETXTBSY 26
#define EFBIG 27
#define ENOSPC 28
#define EPIPE 32
#define ENOTEMPTY 39
#define ENAMETOOLONG 36
#define ENODEV 19
#define ENXIO 6
#define ENFILE 23
#define EMFILE 24
#define EROFS 30
#define EXDEV 18
#define ENOSYS 38
#define EOPNOTSUPP 95
#define ENOTSUP EOPNOTSUPP
#define EOVERFLOW 75
#define ERANGE 34
#define ELOOP 40
#define ESTALE 116
#define EDOM 33
#define EILSEQ 84
#define ETIMEDOUT 110

#endif
