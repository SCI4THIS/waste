/* sys/resource.c — Resource limits for the WASTE guest libc. */

#include "../include/helper.h"

extern void *memset(void *d, i32 c, u32 n);

static i64 resource_limits[32][2];
static i32 resources_ready;
static void init_resources(void){if(resources_ready)return;for(u32 i=0;i<32;i++)resource_limits[i][0]=resource_limits[i][1]=0x7fffffffffffffffLL;resources_ready=1;}
i32 getrlimit(i32 resource,i64*out){init_resources();if(resource<0||resource>=32){*__errno_location()=22;return-1;}out[0]=resource_limits[resource][0];out[1]=resource_limits[resource][1];return 0;}
i32 setrlimit(i32 resource,const i64*in){init_resources();if(resource<0||resource>=32||in[0]>in[1]){*__errno_location()=22;return-1;}resource_limits[resource][0]=in[0];resource_limits[resource][1]=in[1];return 0;}
i32 getrusage(i32 who,void*usage){(void)who;memset(usage,0,144);return 0;}
