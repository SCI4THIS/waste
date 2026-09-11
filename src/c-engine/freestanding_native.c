/*
 * freestanding_native.c — Native Linux x86_64 platform backend.
 *
 * Provides everything needed to run native test binaries without linking
 * libc: memory allocator (mmap-backed), FILE-based I/O (raw syscalls),
 * strtod/strtof, program entry (_start), and misc stubs.
 *
 * This file is linked into native builds only.  The Wasm build uses
 * browser_wast.c instead.
 */

#include <stddef.h>
#include <stdint.h>

/* Declarations from freestanding_lib.c */
int snprintf(char *buf, size_t n, const char *fmt, ...);
int vsnprintf(char *buf, size_t n, const char *fmt, __builtin_va_list ap);
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
size_t strlen(const char *s);
const char *scan_float_end(const char *s);
double ldexp(double x, int exp);

/* Forward declarations for functions defined later in this file */
void *malloc(size_t size);
void  free(void *ptr);

/* ---- Linux x86_64 syscall interface ---- */

static long syscall1(long nr, long a1) {
    long ret;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1)
        : "rcx", "r11", "memory");
    return ret;
}

static long syscall2(long nr, long a1, long a2) {
    long ret;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory");
    return ret;
}

static long syscall3(long nr, long a1, long a2, long a3) {
    long ret;
    register long r10 __asm__("r10") = a3;
    (void)r10;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory");
    return ret;
}

static long syscall6(long nr, long a1, long a2, long a3, long a4,
                     long a5, long a6) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}

/* Syscall numbers (x86_64) */
#define SYS_read        0
#define SYS_write       1
#define SYS_open        2
#define SYS_close       3
#define SYS_fstat       5
#define SYS_lseek       8
#define SYS_mmap        9
#define SYS_munmap      11
#define SYS_ioctl       16
#define SYS_clock_gettime 228
#define SYS_exit_group  231

/* ---- Raw syscall wrappers ---- */

static long sys_write(int fd, const void *buf, size_t count) {
    return syscall3(SYS_write, fd, (long)buf, (long)count);
}

static long sys_read(int fd, void *buf, size_t count) {
    return syscall3(SYS_read, fd, (long)buf, (long)count);
}

static long sys_open(const char *path, int flags, int mode) {
    return syscall3(SYS_open, (long)path, flags, mode);
}

static long sys_close(int fd) {
    return syscall1(SYS_close, fd);
}

struct __kernel_stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    uint64_t st_atime_sec;
    uint64_t st_atime_nsec;
    uint64_t st_mtime_sec;
    uint64_t st_mtime_nsec;
    uint64_t st_ctime_sec;
    uint64_t st_ctime_nsec;
    int64_t  __unused[3];
};

static long sys_fstat(int fd, struct __kernel_stat *buf) {
    return syscall2(SYS_fstat, fd, (long)buf);
}

static long sys_lseek(int fd, long offset, int whence) {
    return syscall3(SYS_lseek, fd, offset, whence);
}

static void *sys_mmap(void *addr, size_t length, int prot, int flags,
                      int fd, long offset) {
    return (void *)syscall6(SYS_mmap, (long)addr, (long)length, prot, flags,
                            fd, offset);
}

static long sys_munmap(void *addr, size_t length) {
    return syscall2(SYS_munmap, (long)addr, (long)length);
}

struct __kernel_timespec {
    long tv_sec;
    long tv_nsec;
};

static long sys_clock_gettime(int clk_id, struct __kernel_timespec *tp) {
    return syscall2(SYS_clock_gettime, clk_id, (long)tp);
}

_Noreturn static void sys_exit_group(int status) {
    syscall1(SYS_exit_group, status);
    __builtin_unreachable();
}

/* ---- Public POSIX-like wrappers ---- */

/* These match the signatures declared in freestanding headers */

int open(const char *path, int flags, ...) {
    /* Simplified: always pass mode 0 (adequate for O_RDONLY) */
    return (int)sys_open(path, flags, 0);
}

int close(int fd) {
    return (int)sys_close(fd);
}

/* fstat: copy from kernel stat to our freestanding stat layout.
 * Both layouts are identical on x86_64 so we can cast directly. */
int fstat(int fd, void *buf) {
    return (int)sys_fstat(fd, (struct __kernel_stat *)buf);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd,
           long offset) {
    return sys_mmap(addr, length, prot, flags, fd, offset);
}

int munmap(void *addr, size_t length) {
    return (int)sys_munmap(addr, length);
}

int clock_gettime(int clk_id, void *tp) {
    return (int)sys_clock_gettime(clk_id, (struct __kernel_timespec *)tp);
}

_Noreturn void exit(int status) {
    sys_exit_group(status);
}

/* ---- Globals ---- */

int errno = 0;
__attribute__((weak)) int yydebug = 0;

/* ---- Stubs ---- */

char *getenv(const char *name) { (void)name; return (void *)0; }

int isatty(int fd) {
    /* Use TIOCGWINSZ ioctl to check */
    char buf[64];
    long ret = syscall3(SYS_ioctl, fd, 0x5413 /* TIOCGWINSZ */, (long)buf);
    return ret == 0 ? 1 : 0;
}

char *strerror(int errnum) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "error %d", errnum);
    return buf;
}

/* ---- FILE-based I/O ---- */

typedef struct {
    int fd;
    int error;
    int eof;
} FILE;

FILE __stdin_file  = { 0, 0, 0 };
FILE __stdout_file = { 1, 0, 0 };
FILE __stderr_file = { 2, 0, 0 };

FILE *fopen(const char *path, const char *mode) {
    int flags = 0;
    if (mode[0] == 'r') flags = 0; /* O_RDONLY */
    else if (mode[0] == 'w') flags = 0x41; /* O_WRONLY | O_CREAT | O_TRUNC */
    else if (mode[0] == 'a') flags = 0x441; /* O_WRONLY | O_CREAT | O_APPEND */
    /* Ignore 'b' and '+' modifiers */

    int fd = (int)sys_open(path, flags, 0644);
    if (fd < 0) return (void *)0;

    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) { sys_close(fd); return (void *)0; }
    f->fd = fd;
    f->error = 0;
    f->eof = 0;
    return f;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f) {
    if (!f || size == 0 || nmemb == 0) return 0;
    size_t total = size * nmemb;
    size_t done = 0;
    unsigned char *p = ptr;
    while (done < total) {
        long n = sys_read(f->fd, p + done, total - done);
        if (n < 0) { f->error = 1; break; }
        if (n == 0) { f->eof = 1; break; }
        done += (size_t)n;
    }
    return done / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f) {
    if (!f || size == 0 || nmemb == 0) return 0;
    size_t total = size * nmemb;
    size_t done = 0;
    const unsigned char *p = ptr;
    while (done < total) {
        long n = sys_write(f->fd, p + done, total - done);
        if (n < 0) { f->error = 1; break; }
        done += (size_t)n;
    }
    return done / size;
}

int fseek(FILE *f, long offset, int whence) {
    if (!f) return -1;
    long ret = sys_lseek(f->fd, offset, whence);
    if (ret < 0) return -1;
    f->eof = 0;
    return 0;
}

long ftell(FILE *f) {
    if (!f) return -1;
    return sys_lseek(f->fd, 0, 1 /* SEEK_CUR */);
}

int fclose(FILE *f) {
    if (!f) return -1;
    int ret = (int)sys_close(f->fd);
    free(f);
    return ret;
}

int ferror(FILE *f) { return f ? f->error : 0; }

int fputc(int c, FILE *f) {
    if (!f) return -1;
    unsigned char ch = (unsigned char)c;
    long n = sys_write(f->fd, &ch, 1);
    return n == 1 ? c : -1;
}

int fputs(const char *s, FILE *f) {
    if (!f) return -1;
    size_t len = strlen(s);
    size_t done = 0;
    while (done < len) {
        long n = sys_write(f->fd, s + done, len - done);
        if (n < 0) { f->error = 1; return -1; }
        done += (size_t)n;
    }
    return 0;
}

int putchar(int c) {
    return fputc(c, &__stdout_file);
}

int getc(FILE *f) {
    if (!f) return -1;
    unsigned char ch;
    long n = sys_read(f->fd, &ch, 1);
    if (n <= 0) { if (n == 0) f->eof = 1; else f->error = 1; return -1; }
    return (int)ch;
}

void clearerr(FILE *f) {
    if (f) { f->error = 0; f->eof = 0; }
}

int fileno(FILE *f) {
    return f ? f->fd : -1;
}

int printf(const char *fmt, ...) {
    char buf[4096];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    if (len > 0) {
        size_t done = 0;
        while (done < (size_t)len) {
            long n = sys_write(1, buf + done, (size_t)len - done);
            if (n < 0) break;
            done += (size_t)n;
        }
    }
    return len;
}

int fprintf(FILE *f, const char *fmt, ...) {
    if (!f) return 0;
    char buf[4096];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    if (len > 0) {
        size_t done = 0;
        while (done < (size_t)len) {
            long n = sys_write(f->fd, buf + done, (size_t)len - done);
            if (n < 0) { f->error = 1; break; }
            done += (size_t)n;
        }
    }
    return len;
}

void perror(const char *s) {
    if (s && *s) {
        fprintf(&__stderr_file, "%s: error\n", s);
    } else {
        fprintf(&__stderr_file, "error\n");
    }
}

/* ---- Memory allocator (mmap-backed) ---- */

/* We use mmap to get memory from the OS.  The allocator algorithm is the
 * same free-list + bump design used by browser_wast.c for Wasm. */

typedef struct heap_block {
    size_t size;
    struct heap_block *next;
    uint32_t is_free;
    uint32_t reserved;
} heap_block;

static heap_block *heap_blocks;
static uintptr_t heap_cursor;
static uintptr_t heap_limit;

static size_t heap_align(size_t size) {
    return (size + 15u) & ~(size_t)15u;
}

static int heap_ensure(size_t amount) {
    /* Ensure at least 'amount' bytes are available from heap_cursor.
     * If the current region is insufficient, mmap a new one. */
    if (heap_cursor + amount <= heap_limit) return 1;
    /* Round up to page boundary (4096) */
    size_t needed = (amount + 4095u) & ~(size_t)4095u;
    /* Request at least 256KB at a time to reduce syscalls */
    if (needed < 262144u) needed = 262144u;
    void *hint = heap_limit ? (void *)heap_limit : (void *)0;
    void *p = sys_mmap(hint, needed,
                       0x3 /* PROT_READ | PROT_WRITE */,
                       0x22 /* MAP_PRIVATE | MAP_ANONYMOUS */,
                       -1, 0);
    if (p == (void *)-1) return 0;
    if ((uintptr_t)p == heap_limit) {
        /* Contiguous extension: just advance the limit */
        heap_limit += needed;
    } else {
        /* Non-contiguous region: move cursor to the new mapping.
         * Existing free-list blocks in the old region remain valid
         * and reachable; new bump allocations come from here. */
        heap_cursor = (uintptr_t)p;
        heap_limit = (uintptr_t)p + needed;
    }
    return 1;
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
    /* Ensure enough space: alignment padding + header + payload */
    size_t total_needed = 16u + header_size + size;
    if (!heap_ensure(total_needed)) return (void *)0;
    uintptr_t start = (heap_cursor + 15u) & ~(uintptr_t)15u;
    uintptr_t limit = start + header_size + size;
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

static int heap_blocks_adjacent(heap_block *a, heap_block *b) {
    /* block payload starts at (a + 1), extends a->size bytes */
    return (unsigned char *)(a + 1) + a->size == (unsigned char *)b;
}

void free(void *ptr) {
    if (!ptr) return;
    heap_block *block = (heap_block *)ptr - 1;
    block->is_free = 1;
    /* Coalesce with following blocks only if physically adjacent */
    while (block->next && block->next->is_free &&
           heap_blocks_adjacent(block, block->next)) {
        block->size += sizeof(heap_block) + block->next->size;
        block->next = block->next->next;
    }
    /* Coalesce with preceding block only if physically adjacent */
    heap_block *previous = (void *)0;
    for (heap_block *at = heap_blocks; at && at != block; at = at->next)
        previous = at;
    if (previous && previous->is_free &&
        heap_blocks_adjacent(previous, block)) {
        previous->size += sizeof(heap_block) + block->size;
        previous->next = block->next;
    }
}

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

/* ---- strtod / strtof (pure C implementation) ---- */

static int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static double parse_hex_float(const char *s, const char *end) {
    /* Parse 0xH.HHHpE format */
    int negative = 0;
    if (*s == '-') { negative = 1; s++; }
    else if (*s == '+') { s++; }

    /* Skip 0x prefix */
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;

    uint64_t mantissa = 0;
    int mantissa_bits = 0;
    int exponent = 0;
    int has_dot = 0;
    int frac_digits = 0;

    /* Parse hex digits before and after dot */
    while (s < end && (hex_digit_value(*s) >= 0 || *s == '.')) {
        if (*s == '.') { has_dot = 1; s++; continue; }
        int digit = hex_digit_value(*s);
        if (mantissa_bits < 60) {
            mantissa = (mantissa << 4) | (uint64_t)digit;
            mantissa_bits += 4;
        } else {
            if (!has_dot) exponent += 4;
        }
        if (has_dot) frac_digits++;
        s++;
    }
    (void)has_dot;

    exponent -= frac_digits * 4;

    /* Parse 'p' exponent */
    if (s < end && (*s == 'p' || *s == 'P')) {
        s++;
        int exp_sign = 1;
        if (s < end && *s == '-') { exp_sign = -1; s++; }
        else if (s < end && *s == '+') { s++; }
        int exp_val = 0;
        while (s < end && *s >= '0' && *s <= '9') {
            exp_val = exp_val * 10 + (*s - '0');
            s++;
        }
        exponent += exp_sign * exp_val;
    }

    if (mantissa == 0) return negative ? -0.0 : 0.0;

    double result = ldexp((double)mantissa, exponent);
    return negative ? -result : result;
}

static double parse_decimal_float(const char *s, const char *end) {
    int negative = 0;
    if (*s == '-') { negative = 1; s++; }
    else if (*s == '+') { s++; }

    /* Accumulate integer part */
    double result = 0.0;
    while (s < end && *s >= '0' && *s <= '9') {
        result = result * 10.0 + (double)(*s - '0');
        s++;
    }

    /* Fractional part */
    if (s < end && *s == '.') {
        s++;
        double place = 0.1;
        while (s < end && *s >= '0' && *s <= '9') {
            result += (double)(*s - '0') * place;
            place *= 0.1;
            s++;
        }
    }

    /* Exponent */
    if (s < end && (*s == 'e' || *s == 'E')) {
        s++;
        int exp_sign = 1;
        if (s < end && *s == '-') { exp_sign = -1; s++; }
        else if (s < end && *s == '+') { s++; }
        int exp_val = 0;
        while (s < end && *s >= '0' && *s <= '9') {
            exp_val = exp_val * 10 + (*s - '0');
            s++;
        }
        /* Apply exponent via repeated multiply/divide */
        int e = exp_val * exp_sign;
        double factor = 10.0;
        if (e < 0) { factor = 0.1; e = -e; }
        while (e > 0) {
            if (e & 1) result *= factor;
            factor *= factor;
            e >>= 1;
        }
    }

    return negative ? -result : result;
}

double strtod(const char *s, char **endptr) {
    const char *start = s;
    const char *number_start;

    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    number_start = s;
    int negative = 0;
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

    /* Reset s to number_start for full parsing */
    s = number_start;
    const char *end = scan_float_end(s[0] == '+' || s[0] == '-' ? s + 1 : s);
    if (end == s || (end == s + 1 && (s[0] == '+' || s[0] == '-'))) {
        if (endptr) *endptr = (char *)start;
        return 0.0;
    }
    if (endptr) *endptr = (char *)end;

    /* Detect hex float */
    const char *digits = (s[0] == '+' || s[0] == '-') ? s + 1 : s;
    if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
        return parse_hex_float(s, end);
    return parse_decimal_float(s, end);
}

float strtof(const char *s, char **endptr) {
    return (float)strtod(s, endptr);
}

/* ---- Program entry point ---- */

int main(int argc, char **argv);

__attribute__((naked, noreturn))
void _start(void) {
    __asm__ volatile (
        "xor %%ebp, %%ebp\n"        /* Clear frame pointer */
        "mov (%%rsp), %%rdi\n"       /* argc = *rsp */
        "lea 8(%%rsp), %%rsi\n"      /* argv = rsp + 8 */
        "call main\n"                /* main(argc, argv) */
        "mov %%eax, %%edi\n"         /* exit status = return value */
        "mov $231, %%eax\n"          /* SYS_exit_group */
        "syscall\n"
        : : : "memory"
    );
}
