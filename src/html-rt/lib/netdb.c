/* netdb.c — Network database stubs for the WASTE guest libc. */

#include "include/helper.h"

extern char *number_text(const char*prefix,i32 number);

i32 getaddrinfo(const char*node,const char*service,const void*hints,void**result){(void)node;(void)service;(void)hints;if(result)*result=0;return-4;}
void freeaddrinfo(void*result){(void)result;}
char *gai_strerror(i32 error){return number_text("address error ",error);}
