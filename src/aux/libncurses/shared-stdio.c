/* A PIC library owns its pointer slots and binds them to the process libc. */
#include <stdio.h>

extern FILE *waste_stdin(void);
extern FILE *waste_stdout(void);
extern FILE *waste_stderr(void);

FILE *stdin;
FILE *stdout;
FILE *stderr;

__attribute__((constructor)) static void waste_shared_stdio_init(void) {
    stdin = waste_stdin();
    stdout = waste_stdout();
    stderr = waste_stderr();
}
