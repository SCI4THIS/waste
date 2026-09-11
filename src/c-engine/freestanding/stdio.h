#ifndef FREESTANDING_STDIO_H
#define FREESTANDING_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct {
    int fd;
    int error;
    int eof;
} FILE;

extern FILE __stdin_file;
extern FILE __stdout_file;
extern FILE __stderr_file;
#define stdin  (&__stdin_file)
#define stdout (&__stdout_file)
#define stderr (&__stderr_file)
#define EOF (-1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int fprintf(FILE *f, const char *fmt, ...);
int printf(const char *fmt, ...);
int snprintf(char *buf, size_t n, const char *fmt, ...);
int vsnprintf(char *buf, size_t n, const char *fmt, __builtin_va_list ap);
int fputc(int c, FILE *f);
int fputs(const char *s, FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f);
int fseek(FILE *f, long offset, int whence);
long ftell(FILE *f);
int fclose(FILE *f);
int ferror(FILE *f);
FILE *fopen(const char *path, const char *mode);
int putchar(int c);
int getc(FILE *f);
void clearerr(FILE *f);
int fileno(FILE *f);
void perror(const char *s);

#endif
