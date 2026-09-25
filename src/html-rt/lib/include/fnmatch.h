#ifndef WASTE_FNMATCH_H
#define WASTE_FNMATCH_H

#define FNM_PATHNAME 0x01
#define FNM_NOESCAPE 0x02
#define FNM_PERIOD 0x04
#define FNM_LEADING_DIR 0x08
#define FNM_CASEFOLD 0x10
#define FNM_FILE_NAME FNM_PATHNAME
#define FNM_EXTMATCH 0x20
#define FNM_NOMATCH 1

int fnmatch(const char *, const char *, int);

#endif
