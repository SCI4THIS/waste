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
int bind(int, const struct sockaddr *, socklen_t);
int listen(int, int);
int accept(int, struct sockaddr *, socklen_t *);
int getsockname(int, struct sockaddr *, socklen_t *);
int shutdown(int, int);
int getsockopt(int, int, int, void *, socklen_t *);
int setsockopt(int, int, int, const void *, socklen_t);
ssize_t send(int, const void *, size_t, int);
ssize_t recv(int, void *, size_t, int);
ssize_t sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
ssize_t recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *);

#endif
