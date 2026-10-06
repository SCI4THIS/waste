#ifndef WASTE_UTIME_H
#define WASTE_UTIME_H

#include <waste/abi/availability.h>

#include <sys/types.h>

struct utimbuf {
  time_t actime;
  time_t modtime;
};

int utime(const char *, const struct utimbuf *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");

#endif
