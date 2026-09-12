/* Wasm stdio stubs (category 2: emulated in-browser) */
#include <stdio.h>

FILE __stdin_file  = { .fd = 0, .error = 0, .eof = 0 };
FILE __stdout_file = { .fd = 1, .error = 0, .eof = 0 };
FILE __stderr_file = { .fd = 2, .error = 0, .eof = 0 };
int fprintf(FILE *f, const char *fmt, ...) { (void)f; (void)fmt; return 0; }
size_t fwrite(const void *p, size_t sz, size_t n, FILE *f) { (void)p; (void)sz; (void)n; (void)f; return 0; }
size_t fread(void *p, size_t sz, size_t n, FILE *f) { (void)p; (void)sz; (void)n; (void)f; return 0; }
int fputc(int c, FILE *f) { (void)c; (void)f; return 0; }
int fputs(const char *s, FILE *f) { (void)s; (void)f; return 0; }
int ferror(FILE *f) { (void)f; return 0; }
FILE *fopen(const char *path, const char *mode) { (void)path; (void)mode; return (void *)0; }
int fseek(FILE *f, long off, int whence) { (void)f; (void)off; (void)whence; return 0; }
long ftell(FILE *f) { (void)f; return 0; }
int fclose(FILE *f) { (void)f; return 0; }
int putchar(int c) { (void)c; return 0; }
int printf(const char *fmt, ...) { (void)fmt; return 0; }
int getc(FILE *f) { (void)f; return -1; }
void clearerr(FILE *f) { (void)f; }
int fileno(FILE *f) { (void)f; return -1; }
void perror(const char *s) { (void)s; }
char *strerror(int n) { (void)n; return "error"; }
