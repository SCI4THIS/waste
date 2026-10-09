/* string.c — String and memory operations for the WASTE guest libc. */

#include "include/helper.h"

u32 strlen(const char*s){return c_length(s);}
u32 strnlen(const char*s,u32 n){u32 i=0;while(i<n&&s[i])i++;return i;}
void *memcpy(void*d,const void*s,u32 n){bytes_copy(d,s,n);return d;}
void *memmove(void*d,const void*s,u32 n){bytes_copy(d,s,n);return d;}
void *memset(void*d,i32 c,u32 n){unsigned char*p=d;for(u32 i=0;i<n;i++)p[i]=(unsigned char)c;return d;}
void *memchr(const void*s,i32 c,u32 n){const unsigned char*p=s;for(u32 i=0;i<n;i++)if(p[i]==(unsigned char)c)return(void*)(p+i);return 0;}
void *rawmemchr(const void*s,i32 c){const unsigned char*p=s;while(*p!=(unsigned char)c)p++;return(void*)p;}
void *mempcpy(void*d,const void*s,u32 n){bytes_copy(d,s,n);return(unsigned char*)d+n;}
i32 memcmp(const void*a,const void*b,u32 n){const unsigned char*x=a,*y=b;for(u32 i=0;i<n;i++)if(x[i]!=y[i])return x[i]-y[i];return 0;}
char *strcpy(char*d,const char*s){if(!valid_pointer(d))return d;if(!valid_pointer(s)){*d=0;return d;}u32 i=0;do d[i]=s[i];while(s[i++]);return d;}
char *stpcpy(char*d,const char*s){u32 i=0;do d[i]=s[i];while(s[i++]);return d+i-1;}
char *strncpy(char*d,const char*s,u32 n){if(!valid_pointer(d))return d;if(!valid_pointer(s)){if(n)*d=0;return d;}u32 i=0;for(;i<n&&s[i];i++)d[i]=s[i];for(;i<n;i++)d[i]=0;return d;}
char *strcat(char*d,const char*s){strcpy(d+c_length(d),s);return d;}
char *strncat(char*d,const char*s,u32 n){char*out=d+c_length(d);u32 i=0;while(i<n&&s[i]){out[i]=s[i];i++;}out[i]=0;return d;}
i32 strcmp(const char*a,const char*b){if(!valid_pointer(a)||!valid_pointer(b))return a==b?0:valid_pointer(a)?1:-1;u32 i=0;while(a[i]&&a[i]==b[i])i++;return(unsigned char)a[i]-(unsigned char)b[i];}
i32 strncmp(const char*a,const char*b,u32 n){if(!valid_pointer(a)||!valid_pointer(b))return a==b?0:valid_pointer(a)?1:-1;for(u32 i=0;i<n;i++){if(a[i]!=b[i]||!a[i])return(unsigned char)a[i]-(unsigned char)b[i];}return 0;}
i32 strcasecmp(const char*a,const char*b){u32 i=0;while(a[i]&&lower_ascii(a[i])==lower_ascii(b[i]))i++;return lower_ascii((unsigned char)a[i])-lower_ascii((unsigned char)b[i]);}
i32 strncasecmp(const char*a,const char*b,u32 n){for(u32 i=0;i<n;i++){i32 x=lower_ascii((unsigned char)a[i]),y=lower_ascii((unsigned char)b[i]);if(x!=y||!x)return x-y;}return 0;}
char *strchr(const char*,i32);
i32 strspn(const char*s,const char*accept){u32 n=0;while(s[n]&&strchr(accept,(unsigned char)s[n]))n++;return n;}
i32 strcspn(const char*s,const char*reject){u32 n=0;while(s[n]&&!strchr(reject,(unsigned char)s[n]))n++;return n;}
i32 mbscasecmp(const char*a,const char*b){return strcasecmp(a,b);}
char *strchr(const char*s,i32 c){do{if((unsigned char)*s==(unsigned char)c)return(char*)s;}while(*s++);return 0;}
char *strchrnul(const char*s,i32 c){while(*s&&(unsigned char)*s!=(unsigned char)c)s++;return(char*)s;}
char *strnul(const char*s){while(*s)s++;return(char*)s;}
char *strrchr(const char*s,i32 c){const char*result=0;do{if((unsigned char)*s==(unsigned char)c)result=s;}while(*s++);return(char*)result;}
char *strpbrk(const char*s,const char*accept){for(;*s;s++)if(strchr(accept,*s))return(char*)s;return 0;}
char *strstr(const char*h,const char*n){if(!*n)return(char*)h;u32 z=c_length(n);for(;*h;h++)if(!strncmp(h,n,z))return(char*)h;return 0;}
char *strcasestr(const char*h,const char*n){if(!*n)return(char*)h;u32 z=c_length(n);for(;*h;h++)if(!strncasecmp(h,n,z))return(char*)h;return 0;}
char *strdup(const char*s){u32 n=c_length(s)+1;char*d=malloc(n);if(d)bytes_copy(d,s,n);return d;}

/* ---- Error/signal name formatting ---- */

char error_text[32];
char *number_text(const char*prefix,i32 number){u32 n=0;while(prefix[n]){error_text[n]=prefix[n];n++;}if(number<0){error_text[n++]='-';number=-number;}char digits[12];u32 d=0;do{digits[d++]=(char)('0'+number%10);number/=10;}while(number);while(d)error_text[n++]=digits[--d];error_text[n]=0;return error_text;}
static const char *error_message(i32 error){
  switch(error){
    case 1:return "Operation not permitted";
    case 2:return "No such file or directory";
    case 4:return "Interrupted system call";
    case 5:return "Input/output error";
    case 7:return "Argument list too long";
    case 8:return "Exec format error";
    case 9:return "Bad file descriptor";
    case 10:return "No child processes";
    case 11:return "Resource temporarily unavailable";
    case 12:return "Cannot allocate memory";
    case 13:return "Permission denied";
    case 14:return "Bad address";
    case 16:return "Device or resource busy";
    case 17:return "File exists";
    case 20:return "Not a directory";
    case 21:return "Is a directory";
    case 22:return "Invalid argument";
    case 24:return "Too many open files";
    case 28:return "No space left on device";
    case 29:return "Illegal seek";
    case 32:return "Broken pipe";
    case 34:return "Numerical result out of range";
    case 38:return "Function not implemented";
    case 39:return "Directory not empty";
    case 40:return "Too many levels of symbolic links";
    case 95:return "Operation not supported";
    default:return 0;
  }
}
char *strerror(i32 error){const char *message=error_message(error);return message?(char*)message:number_text("errno ",error);}
int strerror_r(int error,char *buffer,u32 capacity){
  char *text=strerror(error); u32 n=c_length(text);
  if (!buffer || !capacity) return 14;
  if (n>=capacity) n=capacity-1;
  bytes_copy(buffer,text,n); buffer[n]=0; return 0;
}
char *strsignal(i32 signal){return number_text("signal ",signal);}
i32 __libc_current_sigrtmin(void){return 32;} i32 __libc_current_sigrtmax(void){return 64;}
