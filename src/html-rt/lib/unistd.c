/* unistd.c — POSIX unistd stubs for the WASTE guest libc. */

#include "include/helper.h"

i32 isatty(i32 fd){(void)fd;return 0;}

static char tty_path[9]={'/','d','e','v','/','t','t','y',0};
char *ttyname(i32 descriptor){return descriptor>=0&&descriptor<=2?tty_path:0;}

i32 setdtablesize(i64 size){return size>0&&size<=0x7fffffff?(i32)size:-1;}
i32 sysconf(i32 name){(void)name;return 1024;}
i32 pathconf(const char*path,i32 name){(void)path;(void)name;return 255;}
u32 confstr(i32 name,char*buffer,u32 capacity){(void)name;const char*value="/bin:/usr/bin";u32 needed=c_length(value)+1;if(buffer&&capacity){u32 n=needed<capacity?needed:capacity;bytes_copy(buffer,value,n);buffer[n-1]=0;}return needed;}

i32 execve(const char*p,char*const*a,char*const*e){(void)p;(void)a;(void)e;return unsupported();}
i32 chown(const char*p,u32 u,u32 g){(void)p;(void)u;(void)g;return unsupported();}
i32 readlink(const char*p,char*b,u32 n){(void)p;(void)b;(void)n;return unsupported();}
