/* stdio.c -- Native FILE I/O via raw syscalls. */
#include "include/syscall.h"
#include <stdio.h>

/* Forward declarations for functions from category 1 files */
int snprintf(char *buf, size_t n, const char *fmt, ...);
int vsnprintf(char *buf, size_t n, const char *fmt, __builtin_va_list ap);
size_t strlen(const char *s);
void *malloc(size_t size);
void  free(void *ptr);

/* ---- FILE globals ---- */

FILE __stdin_file  = { 0, 0, 0 };
FILE __stdout_file = { 1, 0, 0 };
FILE __stderr_file = { 2, 0, 0 };

/* ---- strerror ---- */

char *strerror(int errnum) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "error %d", errnum);
    return buf;
}

/* ---- FILE operations ---- */

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
