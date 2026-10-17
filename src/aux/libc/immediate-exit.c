/* Immediate guest termination shares the existing _exit kernel boundary. */
#include <stdlib.h>

extern _Noreturn void waste_env_immediate_exit(int)
  __attribute__((import_module("env"), import_name("_exit")));

_Noreturn void _Exit(int status) {
  waste_env_immediate_exit(status);
}
