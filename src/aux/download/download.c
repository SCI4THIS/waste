/* download.c — Download a VFS file to the host via a browser save dialog.
 *
 * Reads the file from the VFS using standard POSIX calls, then calls
 * the waste_kernel host_download_v1 function which yields the engine
 * and triggers a browser download on the main thread.
 *
 * Usage: download [--verbose] <file>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

__attribute__((import_module("waste_kernel"), import_name("host_download_v1")))
int host_download_v1(const char *name, int name_len,
                     const void *data, int data_len, int flags);

int main(int argc, char **argv, char **envp) {
    (void)envp;
    int verbose = 0;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        if (strcmp(argv[argi], "--verbose") == 0) { verbose = 1; argi++; }
        else break;
    }
    if (argi >= argc) {
        fprintf(stderr, "usage: download [--verbose] <file>\n");
        return 1;
    }

    const char *filepath = argv[argi];
    if (verbose) fprintf(stderr, "download: opening '%s'\n", filepath);

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "download: %s: not found\n", filepath);
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        fprintf(stderr, "download: %s: cannot stat\n", filepath);
        close(fd);
        return 1;
    }

    int file_size = (int)st.st_size;
    if (verbose) fprintf(stderr, "download: file size = %d bytes\n", file_size);
    char *buf = NULL;
    if (file_size > 0) {
        buf = (char *)malloc(file_size);
        if (!buf) {
            fprintf(stderr, "download: out of memory\n");
            close(fd);
            return 1;
        }
        int total = 0;
        while (total < file_size) {
            int n = read(fd, buf + total, file_size - total);
            if (n <= 0) break;
            total += n;
        }
        file_size = total;
    }
    close(fd);

    /* Extract basename for the download filename. */
    const char *name = strrchr(filepath, '/');
    name = name ? name + 1 : filepath;

    int flags = verbose ? 1 : 0;
    if (verbose) fprintf(stderr, "download: calling host_download_v1('%s', %d bytes, flags=%d)\n", name, file_size, flags);
    int result = host_download_v1(name, (int)strlen(name),
                                  buf ? buf : "", file_size, flags);
    free(buf);

    if (result < 0) {
        fprintf(stderr, "download: failed\n");
        return 1;
    }
    if (verbose) fprintf(stderr, "download: complete\n");
    return 0;
}
