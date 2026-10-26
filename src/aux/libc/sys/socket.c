/* Socket capability is unavailable until a runtime broker is provided.
 * Reject requests without touching caller buffers or inventing descriptors. */
#include <sys/socket.h>
#include <errno.h>

static int socket_unsupported(void) { errno = EOPNOTSUPP; return -1; }
int socket(int domain, int type, int protocol) {
    (void)domain; (void)type; (void)protocol; return socket_unsupported();
}
int connect(int fd, const struct sockaddr *address, socklen_t length) {
    (void)fd; (void)address; (void)length; return socket_unsupported();
}
int bind(int fd, const struct sockaddr *address, socklen_t length) {
    (void)fd; (void)address; (void)length; return socket_unsupported();
}
int getpeername(int fd, struct sockaddr *address, socklen_t *length) {
    (void)fd; (void)address; (void)length; return socket_unsupported();
}
int getsockname(int fd, struct sockaddr *address, socklen_t *length) {
    (void)fd; (void)address; (void)length; return socket_unsupported();
}
int accept(int fd, struct sockaddr *address, socklen_t *length) {
    (void)fd; (void)address; (void)length; return socket_unsupported();
}
int listen(int fd, int backlog) {
    (void)fd; (void)backlog; return socket_unsupported();
}
int shutdown(int fd, int how) {
    (void)fd; (void)how; return socket_unsupported();
}
int getsockopt(int fd, int level, int option, void *value, socklen_t *length) {
    (void)fd; (void)level; (void)option; (void)value; (void)length;
    return socket_unsupported();
}
int setsockopt(int fd, int level, int option, const void *value, socklen_t length) {
    (void)fd; (void)level; (void)option; (void)value; (void)length;
    return socket_unsupported();
}
ssize_t send(int fd, const void *bytes, size_t length, int flags) {
    (void)fd; (void)bytes; (void)length; (void)flags; return socket_unsupported();
}
ssize_t recv(int fd, void *bytes, size_t length, int flags) {
    (void)fd; (void)bytes; (void)length; (void)flags; return socket_unsupported();
}
ssize_t sendto(int fd, const void *bytes, size_t length, int flags,
               const struct sockaddr *address, socklen_t address_length) {
    (void)fd; (void)bytes; (void)length; (void)flags; (void)address;
    (void)address_length; return socket_unsupported();
}
ssize_t recvfrom(int fd, void *bytes, size_t length, int flags,
                 struct sockaddr *address, socklen_t *address_length) {
    (void)fd; (void)bytes; (void)length; (void)flags; (void)address;
    (void)address_length; return socket_unsupported();
}
