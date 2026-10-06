#ifndef WASTE_MNTENT_H
#define WASTE_MNTENT_H

#include <waste/abi/availability.h>

#include <stdio.h>

struct mntent {
  char *mnt_fsname;
  char *mnt_dir;
  char *mnt_type;
  char *mnt_opts;
  int mnt_freq;
  int mnt_passno;
};

FILE *setmntent(const char *, const char *) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
struct mntent *getmntent(FILE *) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
int endmntent(FILE *) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");
char *hasmntopt(const struct mntent *, const char *) WASTE_UNAVAILABLE("Package-owned gnulib API; not provided by WASTE guest libc");

#define MNTTYPE_IGNORE "ignore"
#define MNTOPT_IGNORE "ignore"

#endif
