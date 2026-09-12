/* string.c — String and memory operations for the WASTE guest libc. */

#include "common.h"

u32 strlen(const char*s){return c_length(s);}
u32 strnlen(const char*s,u32 n){u32 i=0;while(i<n&&s[i])i++;return i;}
void *memcpy(void*d,const void*s,u32 n){bytes_copy(d,s,n);return d;}
void *memmove(void*d,const void*s,u32 n){bytes_copy(d,s,n);return d;}
void *memset(void*d,i32 c,u32 n){unsigned char*p=d;for(u32 i=0;i<n;i++)p[i]=(unsigned char)c;return d;}
void *memchr(const void*s,i32 c,u32 n){const unsigned char*p=s;for(u32 i=0;i<n;i++)if(p[i]==(unsigned char)c)return(void*)(p+i);return 0;}
i32 memcmp(const void*a,const void*b,u32 n){const unsigned char*x=a,*y=b;for(u32 i=0;i<n;i++)if(x[i]!=y[i])return x[i]-y[i];return 0;}
char *strcpy(char*d,const char*s){if(!valid_pointer(d))return d;if(!valid_pointer(s)){*d=0;return d;}u32 i=0;do d[i]=s[i];while(s[i++]);return d;}
char *strncpy(char*d,const char*s,u32 n){if(!valid_pointer(d))return d;if(!valid_pointer(s)){if(n)*d=0;return d;}u32 i=0;for(;i<n&&s[i];i++)d[i]=s[i];for(;i<n;i++)d[i]=0;return d;}
char *strcat(char*d,const char*s){strcpy(d+c_length(d),s);return d;}
i32 strcmp(const char*a,const char*b){if(!valid_pointer(a)||!valid_pointer(b))return a==b?0:valid_pointer(a)?1:-1;u32 i=0;while(a[i]&&a[i]==b[i])i++;return(unsigned char)a[i]-(unsigned char)b[i];}
i32 strncmp(const char*a,const char*b,u32 n){if(!valid_pointer(a)||!valid_pointer(b))return a==b?0:valid_pointer(a)?1:-1;for(u32 i=0;i<n;i++){if(a[i]!=b[i]||!a[i])return(unsigned char)a[i]-(unsigned char)b[i];}return 0;}
i32 strcasecmp(const char*a,const char*b){u32 i=0;while(a[i]&&lower_ascii(a[i])==lower_ascii(b[i]))i++;return lower_ascii((unsigned char)a[i])-lower_ascii((unsigned char)b[i]);}
i32 strncasecmp(const char*a,const char*b,u32 n){for(u32 i=0;i<n;i++){i32 x=lower_ascii((unsigned char)a[i]),y=lower_ascii((unsigned char)b[i]);if(x!=y||!x)return x-y;}return 0;}
char *strchr(const char*s,i32 c){do{if((unsigned char)*s==(unsigned char)c)return(char*)s;}while(*s++);return 0;}
char *strchrnul(const char*s,i32 c){while(*s&&(unsigned char)*s!=(unsigned char)c)s++;return(char*)s;}
char *strrchr(const char*s,i32 c){const char*result=0;do{if((unsigned char)*s==(unsigned char)c)result=s;}while(*s++);return(char*)result;}
char *strpbrk(const char*s,const char*accept){for(;*s;s++)if(strchr(accept,*s))return(char*)s;return 0;}
char *strstr(const char*h,const char*n){if(!*n)return(char*)h;u32 z=c_length(n);for(;*h;h++)if(!strncmp(h,n,z))return(char*)h;return 0;}
char *strcasestr(const char*h,const char*n){if(!*n)return(char*)h;u32 z=c_length(n);for(;*h;h++)if(!strncasecmp(h,n,z))return(char*)h;return 0;}
char *strdup(const char*s){u32 n=c_length(s)+1;char*d=malloc(n);if(d)bytes_copy(d,s,n);return d;}
