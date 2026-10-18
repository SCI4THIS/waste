#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char word[8];
    memcpy(word, "hello", 6);
    printf("guest-libc: %s %zu\n", word, strlen("four"));
    fflush(stdout);
    write(1, "kernel-write\n", 13);
    return 37;
}
