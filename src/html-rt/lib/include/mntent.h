#ifndef WASTE_MNTENT_H
#define WASTE_MNTENT_H

#include <stdio.h>

struct mntent {
  char *mnt_fsname;
  char *mnt_dir;
  char *mnt_type;
  char *mnt_opts;
  int mnt_freq;
  int mnt_passno;
};

FILE *setmntent(const char *, const char *);
struct mntent *getmntent(FILE *);
int endmntent(FILE *);
char *hasmntopt(const struct mntent *, const char *);

#define MNTTYPE_IGNORE "ignore"
#define MNTOPT_IGNORE "ignore"

#endif
