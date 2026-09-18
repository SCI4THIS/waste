/* sys/select.c — Descriptor readiness stubs for the WASTE guest libc.
 * Readiness is ultimately decided by the engine-owned asynchronous descriptor
 * operation.  Reporting a requested descriptor as ready lets Bash enter
 * read(), where the browser runtime can suspend without blocking the worker. */

#include "../include/helper.h"

i32 select(i32 n,void*r,void*w,void*x,void*t){(void)t;return n>0&&(r||w||x)?1:0;}
i32 pselect(i32 n,void*r,void*w,void*x,const void*t,const void*m){(void)t;(void)m;return n>0&&(r||w||x)?1:0;}
