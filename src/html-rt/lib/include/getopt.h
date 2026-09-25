#ifndef WASTE_GETOPT_H
#define WASTE_GETOPT_H

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

extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;
int getopt(int, char * const *, const char *);
int getopt_long(int, char **, const char *,
                const struct option *, int *);
int getopt_long_only(int, char **, const char *,
                     const struct option *, int *);

#endif
