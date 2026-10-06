#ifndef WASTE_GETOPT_H
#define WASTE_GETOPT_H

#include <waste/abi/availability.h>

/* GNU getopt interface used by coreutils and its gnulib replacement. */
struct option {
  const char *name;
  int has_arg;
  int *flag;
  int val;
};

#define no_argument       0
#define required_argument 1
#define optional_argument 2

/* gnulib's getopt implementation supplies this portability annotation in
   getopt-pfx-ext.h; the freestanding guest has no const argv ABI. */
#define __getopt_argv_const
#define __getopt_argv_const_is_empty 1

extern char *optarg WASTE_UNAVAILABLE("Package-owned getopt state; no guest libc provider");
extern int optind WASTE_UNAVAILABLE("Package-owned getopt state; no guest libc provider");
extern int opterr WASTE_UNAVAILABLE("Package-owned getopt state; no guest libc provider");
extern int optopt WASTE_UNAVAILABLE("Package-owned getopt state; no guest libc provider");
int getopt(int, char * const *, const char *) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
int getopt_long(int, char **, const char *,
                const struct option *, int *) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
int getopt_long_only(int, char **, const char *,
                     const struct option *, int *) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");

#endif
