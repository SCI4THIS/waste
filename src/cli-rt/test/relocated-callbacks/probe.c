/* Observe imported function addresses in static data, before and after main. */
#include <stdio.h>
#include <stdlib.h>
static void (*volatile release_memory)(void *) = free;
static int (*volatile write_message)(const char *) = puts;

__attribute__((constructor)) static void before_main(void) {
    release_memory(malloc(16));
    write_message("constructor");
}
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    release_memory(malloc(32));
    write_message("main");
    return 0;
}
