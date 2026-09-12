#ifndef FREESTANDING_UNISTD_H
#define FREESTANDING_UNISTD_H

#include <stddef.h>

typedef long ssize_t;

int     close(int fd);
int     isatty(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);

#endif
