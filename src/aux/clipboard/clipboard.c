/* Copy guest file text, or a lossless Base64 representation, to the host. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "../../config.h"

__attribute__((import_module("waste_kernel"), import_name("host_clipboard_copy_v1")))
int host_clipboard_copy_v1(const void *data, int length, int flags);

static int usage(int status) {
    fprintf(status ? stderr : stdout,
        "usage: clipboard copy [--base64] [--] FILE\n"
        "Copies text to the clipboard; use --base64 to preserve binary bytes.\n");
    return status;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc == 2 && strcmp(argv[1], "--help") == 0) return usage(0);
    if (argc < 3 || strcmp(argv[1], "copy") != 0) return usage(1);
    int arg = 2, base64 = 0;
    if (arg < argc && strcmp(argv[arg], "--base64") == 0) {
        base64 = 1;
        arg++;
    }
    if (arg < argc && strcmp(argv[arg], "--") == 0) arg++;
    else if (arg < argc && argv[arg][0] == '-') return usage(1);
    if (arg + 1 != argc) return usage(1);
    int fd = open(argv[arg], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "clipboard: %s: cannot open\n", argv[arg]);
        return 1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || (st.st_mode & S_IFMT) != S_IFREG ||
        st.st_size < 0 || st.st_size > CLIPBOARD_MAX_BYTES ||
        (base64 && ((unsigned long long)st.st_size + 2) / 3 * 4 > CLIPBOARD_MAX_BYTES)) {
        fprintf(stderr, "clipboard: %s: requires a regular file within the clipboard size limit\n", argv[arg]);
        close(fd);
        return 1;
    }
    size_t length = (size_t)st.st_size;
    unsigned char *bytes = malloc(length ? length : 1);
    if (!bytes) {
        fprintf(stderr, "clipboard: out of memory\n");
        close(fd);
        return 1;
    }
    size_t total = 0;
    while (total < length) {
        ssize_t count = read(fd, bytes + total, length - total);
        if (count <= 0) {
            fprintf(stderr, "clipboard: %s: cannot read complete file\n", argv[arg]);
            close(fd);
            free(bytes);
            return 1;
        }
        total += (size_t)count;
    }
    close(fd);
    unsigned char *text = bytes;
    size_t text_length = length;
    if (base64) {
        static const char alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        text_length = (length + 2) / 3 * 4;
        text = malloc(text_length ? text_length : 1);
        if (!text) {
            fprintf(stderr, "clipboard: out of memory\n");
            free(bytes);
            return 1;
        }
        size_t out = 0;
        for (size_t i = 0; i < length; i += 3) {
            unsigned a = bytes[i];
            unsigned b = i + 1 < length ? bytes[i + 1] : 0;
            unsigned c = i + 2 < length ? bytes[i + 2] : 0;
            text[out++] = alphabet[a >> 2];
            text[out++] = alphabet[((a & 3) << 4) | (b >> 4)];
            text[out++] = i + 1 < length ? alphabet[((b & 15) << 2) | (c >> 6)] : '=';
            text[out++] = i + 2 < length ? alphabet[c & 63] : '=';
        }
    }
    int result = host_clipboard_copy_v1(text, (int)text_length, 0);
    if (base64) free(text);
    free(bytes);
    if (result < 0) {
        fprintf(stderr, "clipboard: copy cancelled, denied, or unavailable in this runtime\n");
        return 1;
    }
    return 0;
}
