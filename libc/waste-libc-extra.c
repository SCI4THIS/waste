typedef unsigned int u32;
typedef signed int i32;
typedef unsigned long long u64;
typedef signed long long i64;

extern void *malloc(u32);
extern void *realloc(void *, u32);
extern void free(void *);
extern i32 *__errno_location(void);

static u32 length_of(const char *s) { u32 n=0; if(s)while(s[n])n++; return n; }
static i32 lower_ascii(i32 c){return c>='A'&&c<='Z'?c+32:c;}
static void copy_bytes(void *d,const void *s,u32 n){unsigned char *a=d;const unsigned char*b=s;if(a<b)for(u32 i=0;i<n;i++)a[i]=b[i];else while(n){n--;a[n]=b[n];}}

u32 strlen(const char*s){return length_of(s);}
u32 strnlen(const char*s,u32 n){u32 i=0;while(i<n&&s[i])i++;return i;}
void *memcpy(void*d,const void*s,u32 n){copy_bytes(d,s,n);return d;}
void *memmove(void*d,const void*s,u32 n){copy_bytes(d,s,n);return d;}
void *memset(void*d,i32 c,u32 n){unsigned char*p=d;for(u32 i=0;i<n;i++)p[i]=(unsigned char)c;return d;}
void *memchr(const void*s,i32 c,u32 n){const unsigned char*p=s;for(u32 i=0;i<n;i++)if(p[i]==(unsigned char)c)return(void*)(p+i);return 0;}
i32 memcmp(const void*a,const void*b,u32 n){const unsigned char*x=a,*y=b;for(u32 i=0;i<n;i++)if(x[i]!=y[i])return x[i]-y[i];return 0;}
char *strcpy(char*d,const char*s){u32 i=0;do d[i]=s[i];while(s[i++]);return d;}
char *strncpy(char*d,const char*s,u32 n){u32 i=0;for(;i<n&&s[i];i++)d[i]=s[i];for(;i<n;i++)d[i]=0;return d;}
char *strcat(char*d,const char*s){strcpy(d+length_of(d),s);return d;}
i32 strcmp(const char*a,const char*b){u32 i=0;while(a[i]&&a[i]==b[i])i++;return(unsigned char)a[i]-(unsigned char)b[i];}
i32 strncmp(const char*a,const char*b,u32 n){for(u32 i=0;i<n;i++){if(a[i]!=b[i]||!a[i])return(unsigned char)a[i]-(unsigned char)b[i];}return 0;}
i32 strcasecmp(const char*a,const char*b){u32 i=0;while(a[i]&&lower_ascii(a[i])==lower_ascii(b[i]))i++;return lower_ascii((unsigned char)a[i])-lower_ascii((unsigned char)b[i]);}
i32 strncasecmp(const char*a,const char*b,u32 n){for(u32 i=0;i<n;i++){i32 x=lower_ascii((unsigned char)a[i]),y=lower_ascii((unsigned char)b[i]);if(x!=y||!x)return x-y;}return 0;}
char *strchr(const char*s,i32 c){do{if((unsigned char)*s==(unsigned char)c)return(char*)s;}while(*s++);return 0;}
char *strchrnul(const char*s,i32 c){while(*s&&(unsigned char)*s!=(unsigned char)c)s++;return(char*)s;}
char *strrchr(const char*s,i32 c){const char*result=0;do{if((unsigned char)*s==(unsigned char)c)result=s;}while(*s++);return(char*)result;}
char *strpbrk(const char*s,const char*accept){for(;*s;s++)if(strchr(accept,*s))return(char*)s;return 0;}
char *strstr(const char*h,const char*n){if(!*n)return(char*)h;u32 z=length_of(n);for(;*h;h++)if(!strncmp(h,n,z))return(char*)h;return 0;}
char *strcasestr(const char*h,const char*n){if(!*n)return(char*)h;u32 z=length_of(n);for(;*h;h++)if(!strncasecmp(h,n,z))return(char*)h;return 0;}
char *strdup(const char*s){u32 n=length_of(s)+1;char*d=malloc(n);if(d)copy_bytes(d,s,n);return d;}

static i32 digit_value(i32 c){if(c>='0'&&c<='9')return c-'0';c=lower_ascii(c);return c>='a'&&c<='z'?c-'a'+10:-1;}
static u64 parse_unsigned(const char*s,char**end,i32 base,i32*negative){while(*s==' '||*s=='\t'||*s=='\n')s++;*negative=0;if(*s=='+'||*s=='-'){*negative=*s=='-';s++;}if((base==0||base==16)&&s[0]=='0'&&(s[1]=='x'||s[1]=='X')){base=16;s+=2;}else if(base==0)base=*s=='0'?8:10;const char*start=s;u64 value=0;i32 d;while((d=digit_value(*s))>=0&&d<base){u64 next=value*(u32)base+(u32)d;if(next<value){value=~0ULL;*__errno_location()=34;}else value=next;s++;}if(end)*end=(char*)(s==start?start:s);return value;}
i32 strtol(const char*s,char**end,i32 base){i32 neg;u64 v=parse_unsigned(s,end,base,&neg);if(neg)return v>0x80000000ULL?(*__errno_location()=34,0x80000000U):(i32)(0-(u32)v);if(v>0x7fffffffULL){*__errno_location()=34;return 0x7fffffff;}return(i32)v;}
i64 strtoimax(const char*s,char**end,i32 base){i32 neg;u64 v=parse_unsigned(s,end,base,&neg);if(neg)return v>0x8000000000000000ULL?(*__errno_location()=34,(i64)0x8000000000000000ULL):(i64)(0-v);if(v>0x7fffffffffffffffULL){*__errno_location()=34;return 0x7fffffffffffffffLL;}return(i64)v;}
u64 strtoumax(const char*s,char**end,i32 base){i32 neg;u64 v=parse_unsigned(s,end,base,&neg);return neg?0-v:v;}
i32 atoi(const char*s){return strtol(s,0,10);}

static double power10(i32 exponent){double value=1.0;if(exponent>0)while(exponent--)value*=10.0;else while(exponent++)value/=10.0;return value;}
double strtod(const char*s,char**end){while(*s==' '||*s=='\t')s++;i32 neg=0;if(*s=='+'||*s=='-'){neg=*s=='-';s++;}const char*start=s;double value=0;while(*s>='0'&&*s<='9')value=value*10+(*s++-'0');if(*s=='.'){s++;double place=.1;while(*s>='0'&&*s<='9'){value+=(*s++-'0')*place;place*=.1;}}if(*s=='e'||*s=='E'){const char*mark=s++;i32 eneg=0;if(*s=='+'||*s=='-'){eneg=*s=='-';s++;}i32 e=0,any=0;while(*s>='0'&&*s<='9'){any=1;e=e*10+(*s++-'0');}if(any)value*=power10(eneg?-e:e);else s=mark;}if(end)*end=(char*)(s==start?start:s);return neg?-value:value;}

static void double_to_quad(u64*out,double value){union{double d;u64 u;}bits;bits.d=value;u64 sign=bits.u>>63,exp=(bits.u>>52)&0x7ff,frac=bits.u&0xfffffffffffffULL;if(exp==0){out[0]=0;out[1]=sign<<63;return;}if(exp==0x7ff){out[0]=0;out[1]=(sign<<63)|(0x7fffULL<<48)|(frac?1ULL<<47:0);return;}u64 qexp=exp-1023+16383;out[0]=frac<<60;out[1]=(sign<<63)|(qexp<<48)|(frac>>4);}
void strtold(u64*out,const char*s,char**end){double_to_quad(out,strtod(s,end));}
void __floatditf(u64*out,i64 value){u64 sign=value<0,magnitude=sign?(u64)(-(value+1))+1:(u64)value;if(!magnitude){out[0]=out[1]=0;return;}u32 top=0;for(u64 scan=magnitude;scan>>=1;)top++;u64 fraction=magnitude-(1ULL<<top),shift=112-top,low=0,high=0;if(shift>=64)high=fraction<<(shift-64);else{low=fraction<<shift;high=fraction>>(64-shift);}out[0]=low;out[1]=(sign<<63)|((u64)(16383+top)<<48)|high;}
/* Keep this as explicit word arithmetic.  Clang otherwise recognizes the
   usual 32-bit decomposition and lowers it back to a call to __multi3. */
__attribute__((noinline)) static u64 multiply_high(u64 a,u64 b){volatile u64 lo=0,hi=0,mlo=a,mhi=0,bits=b;for(u32 i=0;i<64;i++){if(bits&1){u64 old=lo;lo+=mlo;hi+=mhi+(lo<old);}bits>>=1;mhi=(mhi<<1)|(mlo>>63);mlo<<=1;}return hi;}
void __multi3(u64*out,u64 a0,u64 a1,u64 b0,u64 b1){out[0]=a0*b0;out[1]=multiply_high(a0,b0)+a0*b1+a1*b0;}
void imaxdiv(i64*out,i64 numerator,i64 denominator){out[0]=numerator/denominator;out[1]=numerator%denominator;}

void *sh_malloc(u32 size,const char*file,i32 line){(void)file;(void)line;return malloc(size);}
void *sh_realloc(void*p,u32 size,const char*file,i32 line){(void)file;(void)line;return realloc(p,size);}
void sh_free(void*p,const char*file,i32 line){(void)file;(void)line;free(p);}

static void swap_bytes(unsigned char*a,unsigned char*b,u32 n){while(n--){unsigned char t=*a;*a++=*b;*b++=t;}}
void qsort(void*base,u32 count,u32 size,i32(*compare)(const void*,const void*)){unsigned char*p=base;if(!size)return;for(u32 i=1;i<count;i++)for(u32 j=i;j&&compare(p+(j-1)*size,p+j*size)>0;j--)swap_bytes(p+(j-1)*size,p+j*size,size);}

static i32 glob_match(const char*p,const char*s,i32 insensitive){while(*p){if(*p=='*'){while(*p=='*')p++;if(!*p)return 1;do{if(glob_match(p,s,insensitive))return 1;}while(*s++);return 0;}if(!*s)return 0;if(*p=='?'){p++;s++;continue;}i32 a=(unsigned char)*p++,b=(unsigned char)*s++;if(insensitive){a=lower_ascii(a);b=lower_ascii(b);}if(a!=b)return 0;}return!*s;}
i32 fnmatch(const char*pattern,const char*text,i32 flags){return glob_match(pattern,text,flags&16)?0:1;}

typedef struct WasteRegex{const char*pattern;i32 flags;}WasteRegex;
i32 regcomp(WasteRegex*regex,const char*pattern,i32 flags){regex->pattern=pattern;regex->flags=flags;return 0;}
static i32 regex_here(const char*p,const char*s,i32 icase){if(!*p||(*p=='$'&&!p[1]))return !*p||!*s;if(p[1]=='*'){do{if(regex_here(p+2,s,icase))return 1;}while(*s&&(*p=='.'||(icase?lower_ascii(*p)==lower_ascii(*s):*p==*s))&&s++);return 0;}if(*s&&(*p=='.'||(icase?lower_ascii(*p)==lower_ascii(*s):*p==*s)))return regex_here(p+1,s+1,icase);return 0;}
i32 regexec(const WasteRegex*regex,const char*text,u32 matches,void*match,i32 flags){(void)matches;(void)match;(void)flags;const char*p=regex->pattern;i32 icase=regex->flags&2;if(*p=='^')return regex_here(p+1,text,icase)?0:1;do{if(regex_here(p,text,icase))return 0;}while(*text++);return 1;}
void regfree(WasteRegex*regex){regex->pattern=0;regex->flags=0;}

static u32 random_state=0x6d2b79f5U;
void waste_random_seed(u32 seed){random_state=seed?seed:1;}
u32 arc4random(void){u32 x=random_state;x^=x<<13;x^=x>>17;x^=x<<5;return random_state=x;}
i32 getrandom(void*buffer,u32 count,u32 flags){(void)flags;unsigned char*p=buffer;for(u32 i=0;i<count;i++){if(!(i&3))random_state=arc4random();p[i]=(unsigned char)(random_state>>(8*(i&3)));}return(i32)count;}

static char error_text[32];
static char *number_text(const char*prefix,i32 number){u32 n=0;while(prefix[n]){error_text[n]=prefix[n];n++;}if(number<0){error_text[n++]='-';number=-number;}char digits[12];u32 d=0;do{digits[d++]=(char)('0'+number%10);number/=10;}while(number);while(d)error_text[n++]=digits[--d];error_text[n]=0;return error_text;}
char *strerror(i32 error){return number_text("errno ",error);}
char *strsignal(i32 signal){return number_text("signal ",signal);}
i32 __libc_current_sigrtmin(void){return 32;} i32 __libc_current_sigrtmax(void){return 64;}

static i64 resource_limits[32][2];
static i32 resources_ready;
static void init_resources(void){if(resources_ready)return;for(u32 i=0;i<32;i++)resource_limits[i][0]=resource_limits[i][1]=0x7fffffffffffffffLL;resources_ready=1;}
i32 getrlimit(i32 resource,i64*out){init_resources();if(resource<0||resource>=32){*__errno_location()=22;return-1;}out[0]=resource_limits[resource][0];out[1]=resource_limits[resource][1];return 0;}
i32 setrlimit(i32 resource,const i64*in){init_resources();if(resource<0||resource>=32||in[0]>in[1]){*__errno_location()=22;return-1;}resource_limits[resource][0]=in[0];resource_limits[resource][1]=in[1];return 0;}
i32 getrusage(i32 who,void*usage){(void)who;memset(usage,0,144);return 0;}
i32 setdtablesize(i64 size){return size>0&&size<=0x7fffffff?(i32)size:-1;}
i32 sysconf(i32 name){(void)name;return 1024;}
i32 pathconf(const char*path,i32 name){(void)path;(void)name;return 255;}
u32 confstr(i32 name,char*buffer,u32 capacity){(void)name;const char*value="/bin:/usr/bin";u32 needed=length_of(value)+1;if(buffer&&capacity){u32 n=needed<capacity?needed:capacity;copy_bytes(buffer,value,n);buffer[n-1]=0;}return needed;}

typedef struct WasteTm{i32 sec,min,hour,mday,mon,year,wday,yday,isdst;}WasteTm;
static WasteTm tm_value;
static i32 leap(i32 y){return y%4==0&&(y%100!=0||y%400==0);}
WasteTm *localtime(const i64*timer){i64 seconds=*timer,days=seconds/86400,rest=seconds%86400;if(rest<0){rest+=86400;days--;}tm_value.hour=rest/3600;tm_value.min=(rest/60)%60;tm_value.sec=rest%60;tm_value.wday=(i32)((days+4)%7);if(tm_value.wday<0)tm_value.wday+=7;i32 year=1970;while(days>=(leap(year)?366:365))days-=leap(year++)?366:365;while(days<0){year--;days+=leap(year)?366:365;}tm_value.year=year-1900;tm_value.yday=(i32)days;static const unsigned char month_days[12]={31,28,31,30,31,30,31,31,30,31,30,31};i32 month=0;while(month<11){i32 n=month_days[month]+(month==1&&leap(year));if(days<n)break;days-=n;month++;}tm_value.mon=month;tm_value.mday=(i32)days+1;tm_value.isdst=0;return &tm_value;}
void tzset(void){}
static void two_digits(char*out,i32 v){out[0]=(char)('0'+v/10%10);out[1]=(char)('0'+v%10);}
u32 strftime(char*out,u32 capacity,const char*format,const WasteTm*tm){u32 n=0;for(u32 i=0;format[i];i++){if(format[i]!='%'){if(n+1>=capacity)return 0;out[n++]=format[i];continue;}char c=format[++i];if(c=='Y'){i32 y=tm->year+1900;if(n+4>=capacity)return 0;out[n++]=(char)('0'+y/1000%10);out[n++]=(char)('0'+y/100%10);out[n++]=(char)('0'+y/10%10);out[n++]=(char)('0'+y%10);}else if(c=='m'||c=='d'||c=='H'||c=='M'||c=='S'){if(n+2>=capacity)return 0;i32 v=c=='m'?tm->mon+1:c=='d'?tm->mday:c=='H'?tm->hour:c=='M'?tm->min:tm->sec;two_digits(out+n,v);n+=2;}else if(c=='%'){if(n+1>=capacity)return 0;out[n++]='%';}else return 0;}if(capacity)out[n]=0;return n;}

static u32 temporary_counter;
char *mktemp(char*pattern){u32 n=length_of(pattern),value=++temporary_counter;for(u32 i=n;i&&pattern[i-1]=='X';i--){pattern[i-1]=(char)('a'+value%26);value/=26;}return pattern;}
i32 mkstemp(char*pattern){mktemp(pattern);*__errno_location()=38;return-1;}
char *mkdtemp(char*pattern){mktemp(pattern);*__errno_location()=38;return 0;}

static char tty_path[9]={'/','d','e','v','/','t','t','y',0};
char *ttyname(i32 descriptor){return descriptor>=0&&descriptor<=2?tty_path:0;}
i32 tcflow(i32 descriptor,i32 action){(void)descriptor;(void)action;return 0;}
i32 tgetent(char*buffer,const char*terminal){(void)buffer;(void)terminal;return 1;}
i32 tgetflag(const char*id){(void)id;return 0;} i32 tgetnum(const char*id){(void)id;return-1;}
char *tgetstr(const char*id,char**area){(void)id;(void)area;return 0;}
char *tgoto(const char*capability,i32 column,i32 row){(void)column;(void)row;return(char*)capability;}
i32 tputs(const char*text,i32 lines,i32(*put)(i32)){(void)lines;while(*text)if(put((unsigned char)*text++)<0)return-1;return 0;}

static i32 unsupported(void){*__errno_location()=38;return-1;}
i32 execve(const char*p,char*const*a,char*const*e){(void)p;(void)a;(void)e;return unsupported();}
i32 chown(const char*p,u32 u,u32 g){(void)p;(void)u;(void)g;return unsupported();}
i32 readlink(const char*p,char*b,u32 n){(void)p;(void)b;(void)n;return unsupported();}
void *opendir(const char*p){(void)p;unsupported();return 0;} i32 closedir(void*d){(void)d;return unsupported();} void *readdir(void*d){(void)d;unsupported();return 0;}
i32 select(i32 n,void*r,void*w,void*x,void*t){(void)n;(void)r;(void)w;(void)x;(void)t;return unsupported();}
i32 pselect(i32 n,void*r,void*w,void*x,const void*t,const void*m){(void)n;(void)r;(void)w;(void)x;(void)t;(void)m;return unsupported();}
i32 ioctl(i32 fd,u32 request,void*argument){(void)fd;(void)request;(void)argument;return unsupported();}
i32 socket(i32 domain,i32 type,i32 protocol){(void)domain;(void)type;(void)protocol;return unsupported();}
i32 connect(i32 fd,const void*address,u32 length){(void)fd;(void)address;(void)length;return unsupported();}
i32 getpeername(i32 fd,void*address,u32*length){(void)fd;(void)address;(void)length;return unsupported();}
i32 getaddrinfo(const char*node,const char*service,const void*hints,void**result){(void)node;(void)service;(void)hints;if(result)*result=0;return-4;}
void freeaddrinfo(void*result){(void)result;} char *gai_strerror(i32 error){return number_text("address error ",error);}
void *dlopen(const char*path,i32 flags){(void)path;(void)flags;unsupported();return 0;} void *dlsym(void*handle,const char*name){(void)handle;(void)name;unsupported();return 0;} i32 dlclose(void*handle){(void)handle;return unsupported();} char *dlerror(void){return error_text;}
