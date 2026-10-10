/* Guest stdlib: number parsing, float conversion, arithmetic, sorting. */

/* ---- Guest libc: number parsing, float conversion, arithmetic, sorting,
 * random numbers, and temporary files ---- */
#include "include/helper.h"

int posix_memalign(void **result, size_t alignment, size_t size) {
  if (!result || alignment < sizeof(void *) ||
      (alignment & (alignment - 1)) != 0 || alignment > 16)
    return 22;
  void *memory = malloc((u32)size);
  if (!memory) return 12;
  *result = memory;
  return 0;
}

void *reallocarray(void *pointer, size_t count, size_t size) {
  if (count && size > (size_t)-1 / count) return 0;
  return realloc(pointer, (u32)(count * size));
}

/* ---- Random numbers ---- */

u32 random_state=0x6d2b79f5U;
void waste_random_seed(u32 seed){random_state=seed?seed:1;}
u32 arc4random(void){u32 x=random_state;x^=x<<13;x^=x>>17;x^=x<<5;return random_state=x;}
i32 rand(void){return (i32)(arc4random()&0x7fffffffU);}
void srand(u32 seed){waste_random_seed(seed);}

/* ---- Temporary files ---- */

static u32 temporary_counter;
char *mktemp(char*pattern){u32 n=c_length(pattern),value=++temporary_counter;for(u32 i=n;i&&pattern[i-1]=='X';i--){pattern[i-1]=(char)('a'+value%26);value/=26;}return pattern;}
extern i32 open(const char *path, i32 flags, ...);

static i32 temporary_open(char *pattern, i32 flags) {
  u32 length = c_length(pattern), placeholders = 0;
  while (placeholders < length && pattern[length - placeholders - 1] == 'X')
    placeholders++;
  if (placeholders < 6) { *__errno_location() = 22; return -1; }
  for (u32 attempt = 0; attempt < 256; attempt++) {
    u32 value = ++temporary_counter;
    for (u32 at = 0; at < placeholders; at++) {
      pattern[length - at - 1] = (char)('a' + value % 26);
      value /= 26;
    }
    /* O_RDWR | O_CREAT | O_EXCL.  Exclusive creation makes name selection
       atomic in the engine-owned VFS just as it is on a host filesystem. */
    i32 descriptor = open(pattern, (flags & ~3) | 2 | 0100 | 0200, 0600);
    if (descriptor >= 0) return descriptor;
    if (*__errno_location() != 17) return -1;
  }
  *__errno_location() = 17;
  return -1;
}

i32 mkstemp(char *pattern) { return temporary_open(pattern, 0); }
i32 mkostemp(char *pattern, i32 flags) { return temporary_open(pattern, flags); }
char *mkdtemp(char*pattern){mktemp(pattern);*__errno_location()=38;return 0;}

/* Forward declaration from string.c */
extern void *memset(void *d, i32 c, u32 n);

static i32 digit_value(i32 c){if(c>='0'&&c<='9')return c-'0';c=lower_ascii(c);return c>='a'&&c<='z'?c-'a'+10:-1;}
static u64 parse_unsigned(const char*s,char**end,i32 base,i32*negative){while(*s==' '||*s=='\t'||*s=='\n')s++;*negative=0;if(*s=='+'||*s=='-'){*negative=*s=='-';s++;}if((base==0||base==16)&&s[0]=='0'&&(s[1]=='x'||s[1]=='X')){base=16;s+=2;}else if(base==0)base=*s=='0'?8:10;const char*start=s;u64 value=0;i32 d;while((d=digit_value(*s))>=0&&d<base){u64 next=value*(u32)base+(u32)d;if(next<value){value=~0ULL;*__errno_location()=34;}else value=next;s++;}if(end)*end=(char*)(s==start?start:s);return value;}
i32 strtol(const char*s,char**end,i32 base){i32 neg;u64 v=parse_unsigned(s,end,base,&neg);if(neg)return v>0x80000000ULL?(*__errno_location()=34,0x80000000U):(i32)(0-(u32)v);if(v>0x7fffffffULL){*__errno_location()=34;return 0x7fffffff;}return(i32)v;}
i64 strtoimax(const char*s,char**end,i32 base){i32 neg;u64 v=parse_unsigned(s,end,base,&neg);if(neg)return v>0x8000000000000000ULL?(*__errno_location()=34,(i64)0x8000000000000000ULL):(i64)(0-v);if(v>0x7fffffffffffffffULL){*__errno_location()=34;return 0x7fffffffffffffffLL;}return(i64)v;}
u64 strtoumax(const char*s,char**end,i32 base){i32 neg;u64 v=parse_unsigned(s,end,base,&neg);return neg?0-v:v;}
i32 atoi(const char*s){return strtol(s,0,10);}
i32 abs(i32 value){return value<0?-value:value;}
long atol(const char *text) { return strtol(text, 0, 10); }
long labs(long value) { return value < 0 ? -value : value; }

static double power10(i32 exponent){double value=1.0;if(exponent>0)while(exponent--)value*=10.0;else while(exponent++)value/=10.0;return value;}
double strtod(const char*s,char**end){while(*s==' '||*s=='\t')s++;i32 neg=0;if(*s=='+'||*s=='-'){neg=*s=='-';s++;}const char*start=s;double value=0;while(*s>='0'&&*s<='9')value=value*10+(*s++-'0');if(*s=='.'){s++;double place=.1;while(*s>='0'&&*s<='9'){value+=(*s++-'0')*place;place*=.1;}}if(*s=='e'||*s=='E'){const char*mark=s++;i32 eneg=0;if(*s=='+'||*s=='-'){eneg=*s=='-';s++;}i32 e=0,any=0;while(*s>='0'&&*s<='9'){any=1;e=e*10+(*s++-'0');}if(any)value*=power10(eneg?-e:e);else s=mark;}if(end)*end=(char*)(s==start?start:s);return neg?-value:value;}
float strtof(const char*s,char**end){return (float)strtod(s,end);}

static void double_to_quad(u64*out,double value){union{double d;u64 u;}bits;bits.d=value;u64 sign=bits.u>>63,exp=(bits.u>>52)&0x7ff,frac=bits.u&0xfffffffffffffULL;if(exp==0){out[0]=0;out[1]=sign<<63;return;}if(exp==0x7ff){out[0]=0;out[1]=(sign<<63)|(0x7fffULL<<48)|(frac?1ULL<<47:0);return;}u64 qexp=exp-1023+16383;out[0]=frac<<60;out[1]=(sign<<63)|(qexp<<48)|(frac>>4);}
void __extenddftf2(u64*out,double value){double_to_quad(out,value);}
long double strtold(const char *text, char **end) {
  return (long double)strtod(text, end);
}
void __floatditf(u64*out,i64 value){u64 sign=value<0,magnitude=sign?(u64)(-(value+1))+1:(u64)value;if(!magnitude){out[0]=out[1]=0;return;}u32 top=0;for(u64 scan=magnitude;scan>>=1;)top++;u64 fraction=magnitude-(1ULL<<top),shift=112-top,low=0,high=0;if(shift>=64)high=fraction<<(shift-64);else{low=fraction<<shift;high=fraction>>(64-shift);}out[0]=low;out[1]=(sign<<63)|((u64)(16383+top)<<48)|high;}
/* Keep this as explicit word arithmetic.  Clang otherwise recognizes the
   usual 32-bit decomposition and lowers it back to a call to __multi3. */
__attribute__((noinline)) static u64 multiply_high(u64 a,u64 b){volatile u64 lo=0,hi=0,mlo=a,mhi=0,bits=b;for(u32 i=0;i<64;i++){if(bits&1){u64 old=lo;lo+=mlo;hi+=mhi+(lo<old);}bits>>=1;mhi=(mhi<<1)|(mlo>>63);mlo<<=1;}return hi;}
void __multi3(u64*out,u64 a0,u64 a1,u64 b0,u64 b1){out[0]=a0*b0;out[1]=multiply_high(a0,b0)+a0*b1+a1*b0;}
void imaxdiv(i64*out,i64 numerator,i64 denominator){out[0]=numerator/denominator;out[1]=numerator%denominator;}

char **environ;

void waste_environ_set(char **environment) { environ = environment; }

char *getenv(const char *name) {
  u32 name_length;
  if (!name || !*name || !environ) return 0;
  name_length = c_length(name);
  for (u32 i = 0; environ[i]; i++) {
    char *entry = environ[i];
    u32 at = 0;
    while (at < name_length && entry[at] == name[at]) at++;
    if (at == name_length && entry[at] == '=') return entry + at + 1;
  }
  return 0;
}
char *secure_getenv(const char *name) { return getenv(name); }
i32 setenv(const char *name,const char *value,i32 overwrite){
  (void)name;(void)value;(void)overwrite;return 0;
}
i32 unsetenv(const char *name){(void)name;return 0;}

void *sh_malloc(u32 size,const char*file,i32 line){(void)file;(void)line;return malloc(size);}
void *sh_realloc(void*p,u32 size,const char*file,i32 line){(void)file;(void)line;return realloc(p,size);}
void sh_free(void*p,const char*file,i32 line){(void)file;(void)line;free(p);}

static void swap_bytes(unsigned char*a,unsigned char*b,u32 n){while(n--){unsigned char t=*a;*a++=*b;*b++=t;}}
void qsort(void*base,u32 count,u32 size,i32(*compare)(const void*,const void*)){unsigned char*p=base;if(!size)return;for(u32 i=1;i<count;i++)for(u32 j=i;j&&compare(p+(j-1)*size,p+j*size)>0;j--)swap_bytes(p+(j-1)*size,p+j*size,size);}

void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *)) {
  const unsigned char *bytes = base;
  if (!size || count > (size_t)-1 / size) return 0;
  while (count) {
    size_t middle = count / 2;
    const unsigned char *candidate = bytes + middle * size;
    int order = compare(key, candidate);
    if (!order) return (void *)candidate;
    if (order < 0) count = middle;
    else { bytes = candidate + size; count -= middle + 1; }
  }
  return 0;
}

/* pthread stubs — single-threaded environment. */
i32 pthread_mutex_init(void*m,const void*a){(void)m;(void)a;return 0;}
i32 pthread_mutex_destroy(void*m){(void)m;return 0;}
i32 pthread_mutex_lock(void*m){(void)m;return 0;}
i32 pthread_mutex_unlock(void*m){(void)m;return 0;}
i32 pthread_mutex_trylock(void*m){(void)m;return 0;}

/* signal() — return SIG_DFL; engine owns signal dispatch. */
typedef void (*sighandler_t)(i32);
sighandler_t signal(i32 signum,sighandler_t handler){(void)signum;(void)handler;return (sighandler_t)0;}

/* uselocale — single C.UTF-8 locale. */
void *uselocale(void *locale){(void)locale;return (void *)0;}

/* Compiler runtime — wasm32 long double is double; these convert between
   unsigned 64-bit integers and IEEE-754 quad (128-bit) representation. */
u64 __fixunstfdi(u64 lo,u64 hi){(void)lo;u64 sign=hi>>63;u64 exp=(hi>>48)&0x7fff;if(exp<16383)return 0;u32 shift=(u32)(exp-16383);u64 frac=((hi&0xffffffffffffULL)|0x1000000000000ULL)<<12;if(shift>=64)return sign?0:0xffffffffffffffffULL;return frac>>(64-shift);}
void __floatunditf(u64*out,u64 value){if(!value){out[0]=out[1]=0;return;}u32 top=0;for(u64 scan=value;scan>>=1;)top++;u64 fraction=value-(1ULL<<top),shift=112-top,low=0,high=0;if(shift>=64)high=fraction<<(shift-64);else{low=fraction<<shift;high=fraction>>(64-shift);}out[0]=low;out[1]=((u64)(16383+top)<<48)|high;}
