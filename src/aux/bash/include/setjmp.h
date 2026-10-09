#ifndef WASTE_BASH_SETJMP_H
#define WASTE_BASH_SETJMP_H
/* The evaluator owns checkpoints; the guest address identifies each buffer.
   The first word holds a copyable evaluator token. No native machine
   context or Asyncify state lives in this buffer. */
typedef unsigned int sigjmp_buf[16];
typedef sigjmp_buf jmp_buf;
int sigsetjmp(sigjmp_buf, int) __attribute__((returns_twice));
_Noreturn void siglongjmp(sigjmp_buf, int);
#define setjmp(buffer) sigsetjmp((buffer), 0)
#define longjmp(buffer, value) siglongjmp((buffer), (value))
#endif
