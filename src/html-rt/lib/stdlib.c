/* stdlib.c — Wasm stdlib implementation.
 *
 * WASTE_ENGINE build: JS-hosted float conversion, getenv, exit, heap allocator.
 * Guest libc build:   number parsing, float conversion, arithmetic, sorting.
 */

#ifdef WASTE_ENGINE
/* ---- Engine build: strtod/strtof via JS host, heap allocator ---- */
#include <stddef.h>
#include <stdint.h>

/* Declarations from freestanding_lib.c used by strtod/strtof */
const char *scan_float_end(const char *s);

__attribute__((import_module("waste_host"), import_name("strtod")))
extern double waste_host_strtod(const char *text, size_t length);

__attribute__((import_module("waste_host"), import_name("strtof")))
extern float waste_host_strtof(const char *text, size_t length);

double strtod(const char *s, char **endptr) {
    const char *start = s;
    const char *number_start;
    int negative = 0;

    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    number_start = s;
    if (*s == '-') { negative = 1; s++; }
    else if (*s == '+') { s++; }

    /* Check for inf/nan */
    if ((s[0] == 'i' || s[0] == 'I') && (s[1] == 'n' || s[1] == 'N') &&
        (s[2] == 'f' || s[2] == 'F')) {
        s += 3;
        if ((s[0] == 'i' || s[0] == 'I') && (s[1] == 'n' || s[1] == 'N') &&
            (s[2] == 'i' || s[2] == 'I') && (s[3] == 't' || s[3] == 'T') &&
            (s[4] == 'y' || s[4] == 'Y')) s += 5;
        if (endptr) *endptr = (char *)s;
        return negative ? -__builtin_inf() : __builtin_inf();
    }
    if ((s[0] == 'n' || s[0] == 'N') && (s[1] == 'a' || s[1] == 'A') &&
        (s[2] == 'n' || s[2] == 'N')) {
        s += 3;
        if (*s == '(') { while (*s && *s != ')') s++; if (*s == ')') s++; }
        if (endptr) *endptr = (char *)s;
        return __builtin_nan("");
    }

    s = scan_float_end(s);
    if (s == number_start ||
        (s == number_start + 1 && (*number_start == '+' ||
                                  *number_start == '-'))) {
        if (endptr) *endptr = (char *)start;
        return 0.0;
    }
    if (endptr) *endptr = (char *)s;
    return waste_host_strtod(number_start, (size_t)(s - number_start));
}

float strtof(const char *s, char **endptr) {
    const char *start = s;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    const char *number_start = s;
    if (*s == '+' || *s == '-') s++;
    if ((s[0] == 'i' || s[0] == 'I') ||
        (s[0] == 'n' || s[0] == 'N'))
        return (float)strtod(start, endptr);
    const char *end = scan_float_end(s);
    if (end == s) {
        if (endptr) *endptr = (char *)start;
        return 0.0f;
    }
    if (endptr) *endptr = (char *)end;
    return waste_host_strtof(number_start,
                             (size_t)(end - number_start));
}

char *getenv(const char *name) { (void)name; return (void *)0; }

_Noreturn void exit(int status) { (void)status; __builtin_trap(); }

/* ---- Freestanding heap allocator ---- */
extern unsigned char __heap_base;
static uintptr_t heap_cursor;

typedef struct heap_block {
    size_t size;
    struct heap_block *next;
    uint32_t is_free;
    uint32_t reserved;
} heap_block;

static heap_block *heap_blocks;

static size_t heap_align(size_t size) {
    return (size + 15u) & ~(size_t)15u;
}

static int heap_ensure(uintptr_t limit) {
    uintptr_t memory_size =
        (uintptr_t)__builtin_wasm_memory_size(0) * 65536u;
    if (limit <= memory_size) return 1;
    uintptr_t missing = limit - memory_size;
    uint32_t pages = (uint32_t)((missing + 65535u) / 65536u);
    return __builtin_wasm_memory_grow(0, pages) != (size_t)-1;
}

static void heap_split(heap_block *block, size_t size) {
    const size_t header_size = heap_align(sizeof(heap_block));
    if (block->size < size + header_size + 16u) return;
    heap_block *rest =
        (heap_block *)((unsigned char *)(block + 1) + size);
    rest->size = block->size - size - header_size;
    rest->next = block->next;
    rest->is_free = 1;
    rest->reserved = 0;
    block->size = size;
    block->next = rest;
}

void *malloc(size_t size) {
    const size_t header_size = heap_align(sizeof(heap_block));
    if (size == 0) size = 1;
    if (size > (size_t)-1 - 15u) return (void *)0;
    size = heap_align(size);
    for (heap_block *block = heap_blocks; block; block = block->next) {
        if (block->is_free && block->size >= size) {
            heap_split(block, size);
            block->is_free = 0;
            return block + 1;
        }
    }
    if (heap_cursor == 0) heap_cursor = (uintptr_t)&__heap_base;
    uintptr_t start = (heap_cursor + 15u) & ~(uintptr_t)15u;
    if (start > (uintptr_t)-1 - header_size ||
        size > (uintptr_t)-1 - start - header_size)
        return (void *)0;
    uintptr_t limit = start + header_size + size;
    if (!heap_ensure(limit)) return (void *)0;
    heap_block *block = (heap_block *)start;
    block->size = size;
    block->next = (void *)0;
    block->is_free = 0;
    block->reserved = 0;
    if (!heap_blocks) {
        heap_blocks = block;
    } else {
        heap_block *last = heap_blocks;
        while (last->next) last = last->next;
        last->next = block;
    }
    heap_cursor = limit;
    return block + 1;
}

void *calloc(size_t count, size_t size) {
    unsigned char *result;
    size_t total;
    if (count != 0 && size > (size_t)-1 / count) return (void *)0;
    total = count * size;
    result = (unsigned char *)malloc(total);
    if (!result) return (void *)0;
    for (size_t i = 0; i < total; i++) result[i] = 0;
    return result;
}

void free(void *ptr) {
    if (!ptr) return;
    const size_t header_size = heap_align(sizeof(heap_block));
    heap_block *block = (heap_block *)ptr - 1;
    block->is_free = 1;
    while (block->next && block->next->is_free) {
        block->size += header_size + block->next->size;
        block->next = block->next->next;
    }
    heap_block *previous = (void *)0;
    for (heap_block *at = heap_blocks; at && at != block; at = at->next)
        previous = at;
    if (previous && previous->is_free) {
        previous->size += header_size + block->size;
        previous->next = block->next;
    }
}

/* Forward declaration for use by realloc */
void *memcpy(void *dst, const void *src, size_t n);

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return (void *)0; }
    const size_t header_size = heap_align(sizeof(heap_block));
    heap_block *block = (heap_block *)ptr - 1;
    size_t aligned = heap_align(size);
    if (block->size >= aligned) {
        heap_split(block, aligned);
        return ptr;
    }
    if (block->next && block->next->is_free &&
        block->size + header_size + block->next->size >= aligned) {
        block->size += header_size + block->next->size;
        block->next = block->next->next;
        heap_split(block, aligned);
        return ptr;
    }
    void *fresh = malloc(size);
    if (!fresh) return (void *)0;
    memcpy(fresh, ptr, block->size < size ? block->size : size);
    free(ptr);
    return fresh;
}

#else
/* ---- Guest libc: number parsing, float conversion, arithmetic, sorting ---- */
#include "common.h"

/* Forward declaration from string.c */
extern void *memset(void *d, i32 c, u32 n);

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

#endif /* WASTE_ENGINE */
