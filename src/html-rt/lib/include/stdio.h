#ifndef WASTE_STDIO_H
#define WASTE_STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include <sys/types.h>

/* The FILE layout is private to the guest libc.  The engine-side build only
   needs the descriptor fields used by its compatibility stubs; applications
   must use the standard functions below rather than inspect FILE directly. */
typedef struct waste_stdio_file {
  int fd;
  int error;
  int eof;
} FILE;

#define EOF (-1)

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int fprintf(FILE *, const char *, ...);
int vfprintf(FILE *, const char *, va_list);
int printf(const char *, ...);
int vprintf(const char *, va_list);
int vasprintf(char **, const char *, va_list);
int snprintf(char *, size_t, const char *, ...);
ptrdiff_t vaszprintf(char **, const char *, va_list);
ptrdiff_t aszprintf(char **, const char *, ...);
off64_t vfzprintf(FILE *, const char *, va_list);
off64_t vzprintf(const char *, va_list);
ptrdiff_t vsnzprintf(char *, size_t, const char *, va_list);
ptrdiff_t vszprintf(char *, const char *, va_list);
int sprintf(char *, const char *, ...);
int fputc(int, FILE *);
int putc(int, FILE *);
int putchar(int);
int fputs(const char *, FILE *);
int puts(const char *);
size_t fwrite(const void *, size_t, size_t, FILE *);
size_t fread(void *, size_t, size_t, FILE *);
ssize_t getdelim(char **, size_t *, int, FILE *);
ssize_t getline(char **, size_t *, FILE *);
int ferror(FILE *);
void clearerr(FILE *);
int feof(FILE *);
int fileno(FILE *);
size_t __freadahead(FILE *);
int __freading(FILE *);
int __fwriting(FILE *);
void __fpurge(FILE *);
void __fseterr(FILE *);
FILE *fopen(const char *, const char *);
FILE *fdopen(int, const char *);
FILE *freopen(const char *, const char *, FILE *);
int fclose(FILE *);
int fflush(FILE *);
int setvbuf(FILE *, char *, int, size_t);
int fpurge(FILE *);
int fseek(FILE *, long, int);
int fseeko(FILE *, off_t, int);
long ftell(FILE *);
off_t ftello(FILE *);
int getc(FILE *);
int fgetc(FILE *);
int getchar(void);
void perror(const char *);
const char *strerror(int);

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define BUFSIZ 4096
#ifndef WEOF
# define WEOF ((unsigned int)-1)
#endif
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

#endif
