#ifndef ROGUE_BOOL_FIX_H
#define ROGUE_BOOL_FIX_H
#include <stdbool.h>
#include <curses.h>
#undef bool
#define bool NCURSES_BOOL
#endif
