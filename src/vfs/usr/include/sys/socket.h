#ifndef WASTE_SYS_SOCKET_H
#define WASTE_SYS_SOCKET_H

#include <sys/types.h>

typedef unsigned short sa_family_t;
struct sockaddr { sa_family_t sa_family; char sa_data[14]; };
struct sockaddr_storage { sa_family_t ss_family; char __data[126]; };

#define AF_UNSPEC 0
#define AF_INET 2
#define AF_INET6 10
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

int socket(int, int, int);
int connect(int, const struct sockaddr *, socklen_t);
int getpeername(int, struct sockaddr *, socklen_t *);

#endif
