/* sys/random.c — Random byte generation for the WASTE guest libc. */

#include "../include/helper.h"

extern u32 random_state;
extern u32 arc4random(void);

i32 getrandom(void*buffer,u32 count,u32 flags){(void)flags;unsigned char*p=buffer;for(u32 i=0;i<count;i++){if(!(i&3))random_state=arc4random();p[i]=(unsigned char)(random_state>>(8*(i&3)));}return(i32)count;}
