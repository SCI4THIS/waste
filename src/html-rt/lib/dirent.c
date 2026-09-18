/* dirent.c — Directory traversal stubs for the WASTE guest libc. */

#include "include/helper.h"

void *opendir(const char*p){(void)p;unsupported();return 0;}
i32 closedir(void*d){(void)d;return unsupported();}
void *readdir(void*d){(void)d;unsupported();return 0;}
