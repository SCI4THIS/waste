#ifndef WASTE_GRP_H
#define WASTE_GRP_H

#include <sys/types.h>

struct group {
  char *gr_name;
  char *gr_passwd;
  gid_t gr_gid;
  char **gr_mem;
};

struct group *getgrgid(gid_t);
struct group *getgrnam(const char *);
struct group *getgrent(void);
int getgrouplist(const char *, gid_t, gid_t *, int *);
void setgrent(void);
void endgrent(void);

#endif
