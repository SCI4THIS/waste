#ifndef WASTE_DIRENT_H
#define WASTE_DIRENT_H

#include <waste/abi/availability.h>

#include "sys/types.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef struct waste_dir DIR;
struct dirent {
  /* Match the prebuilt Bash/Emscripten wasm32 ABI.  These two fields remain
     64-bit even though C long and ordinary guest pointers are 32-bit. */
  unsigned long long d_ino;
  signed long long d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[256];
};

_Static_assert(__builtin_offsetof(struct dirent, d_name) == 19,
               "wasm32 dirent d_name offset must match Bash");

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
DIR *fdopendir(int) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int dirfd(DIR *);
int closedir(DIR *);
struct dirent *readdir(DIR *);
void rewinddir(DIR *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");

#endif
