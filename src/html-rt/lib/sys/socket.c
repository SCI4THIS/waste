/* sys/socket.c — Socket stubs for the WASTE guest libc. */

#include "../include/helper.h"

i32 socket(i32 domain,i32 type,i32 protocol){(void)domain;(void)type;(void)protocol;return unsupported();}
i32 connect(i32 fd,const void*address,u32 length){(void)fd;(void)address;(void)length;return unsupported();}
i32 getpeername(i32 fd,void*address,u32*length){(void)fd;(void)address;(void)length;return unsupported();}
