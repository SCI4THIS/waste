/* dlfcn.c — Dynamic loading stubs for the WASTE guest libc. */

#include "include/helper.h"

extern char error_text[32];

void *dlopen(const char*path,i32 flags){(void)path;(void)flags;unsupported();return 0;}
void *dlsym(void*handle,const char*name){(void)handle;(void)name;unsupported();return 0;}
i32 dlclose(void*handle){(void)handle;return unsupported();}
char *dlerror(void){return error_text;}
