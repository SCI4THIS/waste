#ifndef WASTE_ARPA_INET_H
#define WASTE_ARPA_INET_H

#include <sys/types.h>
#include <byteswap.h>

#define htons(value) waste_byteswap16((unsigned short)(value))
#define ntohs(value) waste_byteswap16((unsigned short)(value))
#define htonl(value) waste_byteswap32((unsigned int)(value))
#define ntohl(value) waste_byteswap32((unsigned int)(value))

int inet_aton(const char *, void *);
unsigned int inet_addr(const char *);
const char *inet_ntop(int, const void *, char *, size_t);
int inet_pton(int, const char *, void *);

#endif
