#ifndef FREESTANDING_TIME_H
#define FREESTANDING_TIME_H

#define CLOCK_MONOTONIC 1

struct timespec {
    long tv_sec;
    long tv_nsec;
};

int clock_gettime(int clk_id, struct timespec *tp);

#endif
