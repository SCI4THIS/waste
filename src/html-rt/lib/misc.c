/* misc.c — Random number generation, error strings, resource limits,
 * time formatting, temporary files, terminal stubs, and unsupported POSIX
 * stubs for the WASTE guest libc. */

#include "common.h"

/* Forward declaration from string.c */
extern void *memset(void *d, i32 c, u32 n);

/* ---- Random numbers ---- */

static u32 random_state=0x6d2b79f5U;
void waste_random_seed(u32 seed){random_state=seed?seed:1;}
u32 arc4random(void){u32 x=random_state;x^=x<<13;x^=x>>17;x^=x<<5;return random_state=x;}
i32 getrandom(void*buffer,u32 count,u32 flags){(void)flags;unsigned char*p=buffer;for(u32 i=0;i<count;i++){if(!(i&3))random_state=arc4random();p[i]=(unsigned char)(random_state>>(8*(i&3)));}return(i32)count;}

/* ---- Error strings ---- */

static char error_text[32];
static char *number_text(const char*prefix,i32 number){u32 n=0;while(prefix[n]){error_text[n]=prefix[n];n++;}if(number<0){error_text[n++]='-';number=-number;}char digits[12];u32 d=0;do{digits[d++]=(char)('0'+number%10);number/=10;}while(number);while(d)error_text[n++]=digits[--d];error_text[n]=0;return error_text;}
char *strerror(i32 error){return number_text("errno ",error);}
char *strsignal(i32 signal){return number_text("signal ",signal);}
i32 __libc_current_sigrtmin(void){return 32;} i32 __libc_current_sigrtmax(void){return 64;}

/* ---- Resource limits ---- */

static i64 resource_limits[32][2];
static i32 resources_ready;
static void init_resources(void){if(resources_ready)return;for(u32 i=0;i<32;i++)resource_limits[i][0]=resource_limits[i][1]=0x7fffffffffffffffLL;resources_ready=1;}
i32 getrlimit(i32 resource,i64*out){init_resources();if(resource<0||resource>=32){*__errno_location()=22;return-1;}out[0]=resource_limits[resource][0];out[1]=resource_limits[resource][1];return 0;}
i32 setrlimit(i32 resource,const i64*in){init_resources();if(resource<0||resource>=32||in[0]>in[1]){*__errno_location()=22;return-1;}resource_limits[resource][0]=in[0];resource_limits[resource][1]=in[1];return 0;}
i32 getrusage(i32 who,void*usage){(void)who;memset(usage,0,144);return 0;}
i32 setdtablesize(i64 size){return size>0&&size<=0x7fffffff?(i32)size:-1;}
i32 sysconf(i32 name){(void)name;return 1024;}
i32 pathconf(const char*path,i32 name){(void)path;(void)name;return 255;}
u32 confstr(i32 name,char*buffer,u32 capacity){(void)name;const char*value="/bin:/usr/bin";u32 needed=c_length(value)+1;if(buffer&&capacity){u32 n=needed<capacity?needed:capacity;bytes_copy(buffer,value,n);buffer[n-1]=0;}return needed;}

/* ---- Time ---- */

typedef struct WasteTm{i32 sec,min,hour,mday,mon,year,wday,yday,isdst;}WasteTm;
static WasteTm tm_value;
static i32 leap(i32 y){return y%4==0&&(y%100!=0||y%400==0);}
WasteTm *localtime(const i64*timer){i64 seconds=*timer,days=seconds/86400,rest=seconds%86400;if(rest<0){rest+=86400;days--;}tm_value.hour=rest/3600;tm_value.min=(rest/60)%60;tm_value.sec=rest%60;tm_value.wday=(i32)((days+4)%7);if(tm_value.wday<0)tm_value.wday+=7;i32 year=1970;while(days>=(leap(year)?366:365))days-=leap(year++)?366:365;while(days<0){year--;days+=leap(year)?366:365;}tm_value.year=year-1900;tm_value.yday=(i32)days;static const unsigned char month_days[12]={31,28,31,30,31,30,31,31,30,31,30,31};i32 month=0;while(month<11){i32 n=month_days[month]+(month==1&&leap(year));if(days<n)break;days-=n;month++;}tm_value.mon=month;tm_value.mday=(i32)days+1;tm_value.isdst=0;return &tm_value;}
void tzset(void){}
static void two_digits(char*out,i32 v){out[0]=(char)('0'+v/10%10);out[1]=(char)('0'+v%10);}
u32 strftime(char*out,u32 capacity,const char*format,const WasteTm*tm){u32 n=0;for(u32 i=0;format[i];i++){if(format[i]!='%'){if(n+1>=capacity)return 0;out[n++]=format[i];continue;}char c=format[++i];if(c=='Y'){i32 y=tm->year+1900;if(n+4>=capacity)return 0;out[n++]=(char)('0'+y/1000%10);out[n++]=(char)('0'+y/100%10);out[n++]=(char)('0'+y/10%10);out[n++]=(char)('0'+y%10);}else if(c=='m'||c=='d'||c=='H'||c=='M'||c=='S'){if(n+2>=capacity)return 0;i32 v=c=='m'?tm->mon+1:c=='d'?tm->mday:c=='H'?tm->hour:c=='M'?tm->min:tm->sec;two_digits(out+n,v);n+=2;}else if(c=='%'){if(n+1>=capacity)return 0;out[n++]='%';}else return 0;}if(capacity)out[n]=0;return n;}

/* ---- Temporary files ---- */

static u32 temporary_counter;
char *mktemp(char*pattern){u32 n=c_length(pattern),value=++temporary_counter;for(u32 i=n;i&&pattern[i-1]=='X';i--){pattern[i-1]=(char)('a'+value%26);value/=26;}return pattern;}
i32 mkstemp(char*pattern){mktemp(pattern);*__errno_location()=38;return-1;}
char *mkdtemp(char*pattern){mktemp(pattern);*__errno_location()=38;return 0;}

/* ---- Terminal ---- */

static char tty_path[9]={'/','d','e','v','/','t','t','y',0};
char *ttyname(i32 descriptor){return descriptor>=0&&descriptor<=2?tty_path:0;}
i32 tcflow(i32 descriptor,i32 action){(void)descriptor;(void)action;return 0;}
i32 tgetent(char*buffer,const char*terminal){(void)buffer;(void)terminal;return 1;}
i32 tgetflag(const char*id){(void)id;return 0;} i32 tgetnum(const char*id){(void)id;return-1;}
char *tgetstr(const char*id,char**area){(void)id;(void)area;return 0;}
char *tgoto(const char*capability,i32 column,i32 row){(void)column;(void)row;return(char*)capability;}
i32 tputs(const char*text,i32 lines,i32(*put)(i32)){(void)lines;while(*text)if(put((unsigned char)*text++)<0)return-1;return 0;}

/* ---- Unsupported POSIX stubs ---- */

static i32 unsupported(void){*__errno_location()=38;return-1;}
i32 execve(const char*p,char*const*a,char*const*e){(void)p;(void)a;(void)e;return unsupported();}
i32 chown(const char*p,u32 u,u32 g){(void)p;(void)u;(void)g;return unsupported();}
i32 readlink(const char*p,char*b,u32 n){(void)p;(void)b;(void)n;return unsupported();}
void *opendir(const char*p){(void)p;unsupported();return 0;} i32 closedir(void*d){(void)d;return unsupported();} void *readdir(void*d){(void)d;unsupported();return 0;}
/* Readiness is ultimately decided by the engine-owned asynchronous descriptor
   operation.  Reporting a requested descriptor as ready lets Bash enter
   read(), where the browser runtime can suspend without blocking the worker. */
i32 select(i32 n,void*r,void*w,void*x,void*t){(void)t;return n>0&&(r||w||x)?1:0;}
i32 pselect(i32 n,void*r,void*w,void*x,const void*t,const void*m){(void)t;(void)m;return n>0&&(r||w||x)?1:0;}
i32 ioctl(i32 fd,u32 request,void*argument){(void)fd;(void)request;(void)argument;return unsupported();}
i32 socket(i32 domain,i32 type,i32 protocol){(void)domain;(void)type;(void)protocol;return unsupported();}
i32 connect(i32 fd,const void*address,u32 length){(void)fd;(void)address;(void)length;return unsupported();}
i32 getpeername(i32 fd,void*address,u32*length){(void)fd;(void)address;(void)length;return unsupported();}
i32 getaddrinfo(const char*node,const char*service,const void*hints,void**result){(void)node;(void)service;(void)hints;if(result)*result=0;return-4;}
void freeaddrinfo(void*result){(void)result;} char *gai_strerror(i32 error){return number_text("address error ",error);}
void *dlopen(const char*path,i32 flags){(void)path;(void)flags;unsupported();return 0;} void *dlsym(void*handle,const char*name){(void)handle;(void)name;unsupported();return 0;} i32 dlclose(void*handle){(void)handle;return unsupported();} char *dlerror(void){return error_text;}
