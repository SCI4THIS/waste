#ifndef WASTE_DIRENT_H
#define WASTE_DIRENT_H

#include "sys/types.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef struct waste_dir DIR;
struct dirent {
  ino_t d_ino;
  off_t d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[256];
};

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14
#define _GL_DT_NOTDIR 0x100
#define IFTODT(mode) (((mode) & 0170000) >> 12)
#define DTTOIF(type) ((type) << 12)

DIR *opendir(const char *);
DIR *fdopendir(int);
int dirfd(DIR *);
int closedir(DIR *);
struct dirent *readdir(DIR *);
void rewinddir(DIR *);

#endif
