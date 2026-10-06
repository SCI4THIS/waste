#ifndef WASTE_SCHED_H
#define WASTE_SCHED_H

#include <waste/abi/availability.h>

struct sched_param {
  int sched_priority;
};

#define SCHED_OTHER 0
#define SCHED_FIFO 1
#define SCHED_RR 2

int sched_yield(void) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int sched_get_priority_min(int) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int sched_get_priority_max(int) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");

#endif
