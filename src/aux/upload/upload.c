/* upload.c — Copy a host file into the browser VFS via a file picker.
 *
 * Calls the waste_kernel host_upload_v1 function which yields the
 * engine, triggers a browser file picker on the main thread, and
 * resumes with the selected file data written to the VFS.
 *
 * Usage: upload [--verbose] <dest-path>
 */

#include <stdio.h>
#include <string.h>

__attribute__((import_module("waste_kernel"), import_name("host_upload_v1")))
int host_upload_v1(const char *path, int path_len, int flags);

int main(int argc, char **argv, char **envp) {
    (void)envp;
    int verbose = 0;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        if (strcmp(argv[argi], "--verbose") == 0) { verbose = 1; argi++; }
        else break;
    }
    if (argi >= argc) {
        fprintf(stderr, "usage: upload [--verbose] <dest-path>\n");
        return 1;
    }
    const char *path = argv[argi];
    int flags = verbose ? 1 : 0;
    if (verbose) fprintf(stderr, "upload: requesting upload to '%s' (flags=%d)\n", path, flags);
    int result = host_upload_v1(path, (int)strlen(path), flags);
    if (result < 0) {
        fprintf(stderr, "upload: cancelled\n");
        return 1;
    }
    printf("uploaded %d bytes to %s\n", result, path);
    return 0;
}
