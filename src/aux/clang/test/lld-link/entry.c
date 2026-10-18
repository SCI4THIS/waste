__attribute__((import_module("libc"), import_name("exit"), noreturn)) void guest_exit(int);
extern int answer(void);
void _start(void) { guest_exit(answer()); }
