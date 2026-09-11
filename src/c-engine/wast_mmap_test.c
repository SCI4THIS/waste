#include "wast_runner.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static double elapsed_ms(const struct timespec *a, const struct timespec *b) {
    return (double)(b->tv_sec - a->tv_sec) * 1000.0 +
           (double)(b->tv_nsec - a->tv_nsec) / 1000000.0;
}

static int check_file(const char *path, size_t *bytes_out, double *ms_out) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) { perror(path); return 1; }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 0) {
        perror(path); close(fd); return 1;
    }
    size_t length = (size_t)st.st_size;
    void *mapped = NULL;
    if (length != 0) {
        mapped = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped == MAP_FAILED) { perror(path); close(fd); return 1; }
    }
    struct timespec begin, end;
    clock_gettime(CLOCK_MONOTONIC, &begin);
    wast_script *script = (wast_script *)calloc(1, sizeof(*script));
    if (!script) {
        fprintf(stderr, "FAIL %s: out of memory\n", path);
        if (mapped) munmap(mapped, length);
        close(fd);
        return 1;
    }
    int rc = wast_parse_bytes((const char *)(length ? mapped : ""), length, script);
    clock_gettime(CLOCK_MONOTONIC, &end);
    if (mapped) munmap(mapped, length);
    close(fd);
    *bytes_out = length;
    *ms_out = elapsed_ms(&begin, &end);
    if (rc != 0) {
        fprintf(stderr, "FAIL %s: %s\n", path,
                script->error[0] ? script->error : "parse failed");
        wast_script_free(script);
        free(script);
        return 1;
    }
    printf("PASS %s (%zu bytes, %.3f ms, %d commands, %d module group%s, %d assertions)\n",
           path, length, *ms_out, script->command_count, script->group_count,
           script->group_count == 1 ? "" : "s", script->assertion_count);
    wast_script_free(script);
    free(script);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s file.wast [...]\n", argv[0]);
        return 2;
    }
    int failures = 0;
    size_t total_bytes = 0;
    double total_ms = 0.0;
    for (int i = 1; i < argc; i++) {
        size_t bytes = 0;
        double ms = 0.0;
        failures += check_file(argv[i], &bytes, &ms);
        total_bytes += bytes;
        total_ms += ms;
    }
    printf("SUMMARY files=%d failures=%d bytes=%zu parse_ms=%.3f\n",
           argc - 1, failures, total_bytes, total_ms);
    return failures ? 1 : 0;
}
