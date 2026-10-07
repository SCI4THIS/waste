/* netdb.c — Network database stubs for the WASTE guest libc. */

#include "include/helper.h"

struct addrinfo;

extern char *number_text(const char*prefix,i32 number);

i32 getaddrinfo(const char*node,const char*service,const struct addrinfo*hints,struct addrinfo**result){(void)node;(void)service;(void)hints;if(result)*result=0;return-4;}
void freeaddrinfo(struct addrinfo*result){(void)result;}
char *gai_strerror(i32 error){return number_text("address error ",error);}
