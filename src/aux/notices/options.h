/* Optional guest-command notices. Included only by opted-in application CRTs. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef WASTE_NOTICE_LICENSE_PATH
#define WASTE_NOTICE_LICENSE_PATH "/usr/share/licenses/" WASTE_NOTICE_PACKAGE "/" WASTE_NOTICE_LICENSE
#endif

#ifdef WASTE_SHARED_LIBC_IMPORTS
extern __typeof__(open) open __attribute__((import_module("libc")));
extern __typeof__(close) close __attribute__((import_module("libc")));
extern __typeof__(fprintf) fprintf __attribute__((import_module("libc")));
extern __typeof__(strcmp) strcmp __attribute__((import_module("libc")));
extern __typeof__(__errno_location) __errno_location __attribute__((import_module("libc")));
#endif

/* Return -1 for ordinary application arguments; otherwise an exit status.
   A fixed build-time package keeps notices correct when argv[0] is changed. */
static int waste_command_notice(int argc, char **argv) {
  const char *path;
  char buffer[128];
  int fd, status = 0;
  if (argc != 2) return -1;
  if (strcmp(argv[1], "--license") == 0)
    path = WASTE_NOTICE_LICENSE_PATH;
  else if (strcmp(argv[1], "--authors") == 0)
    path = "/usr/share/licenses/" WASTE_NOTICE_PACKAGE "/AUTHORS";
  else
    return -1;
#ifdef WASTE_NOTICE_ECHO
  /* Match GNU echo's existing informational options in POSIX mode. */
  if (getenv("POSIXLY_CORRECT")) return -1;
#endif
  fd = open(path, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "%s: cannot open %s\n", argv[0], path);
    return 1;
  }
  for (;;) {
    ssize_t count = read(fd, buffer, sizeof buffer);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) { status = 1; break; }
    if (!count) break;
    for (ssize_t offset = 0; offset < count;) {
      ssize_t written = write(STDOUT_FILENO, buffer + offset, count - offset);
      if (written < 0 && errno == EINTR) continue;
      if (written <= 0) { status = 1; break; }
      offset += written;
    }
    if (status) break;
  }
  if (close(fd) < 0) status = 1;
  if (status) fprintf(stderr, "%s: cannot copy %s to stdout\n", argv[0], path);
#ifdef WASTE_NOTICE_FALSE
  /* GNU false retains failure status for informational options. */
  return 1;
#else
  return status;
#endif
}
