#include <stdio.h>

/* SDK programs own their arguments; command notice extensions must not leak. */
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    return puts(argv[1]) < 0;
}
