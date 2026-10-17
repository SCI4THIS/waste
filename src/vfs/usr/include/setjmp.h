#ifndef WASTE_SETJMP_H
#define WASTE_SETJMP_H

/* The first word is an opaque, copyable evaluator checkpoint token. The
 * remaining words reserve the existing Bash ABI, not native machine state.
 * The caller module must export its mutable i32 __stack_pointer. A nonzero
 * sigsetjmp save_mask argument saves/restores the engine-owned process mask. */
typedef unsigned int sigjmp_buf[16];
typedef sigjmp_buf jmp_buf;

#ifdef __cplusplus
extern "C" {
#define WASTE_JUMP_NORETURN [[noreturn]]
#else
#define WASTE_JUMP_NORETURN _Noreturn
#endif
int sigsetjmp(sigjmp_buf, int) __attribute__((returns_twice));
WASTE_JUMP_NORETURN void siglongjmp(sigjmp_buf, int);
WASTE_JUMP_NORETURN void longjmp(jmp_buf, int);
#ifdef __cplusplus
}
#endif
#undef WASTE_JUMP_NORETURN

/* Keep the checkpoint call in the caller's activation. A C wrapper function
 * would return before longjmp and invalidate that activation's checkpoint. */
#define setjmp(buffer) sigsetjmp((buffer), 0)

#endif
