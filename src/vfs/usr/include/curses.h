#ifndef WASTE_CURSES_H
#define WASTE_CURSES_H
/* The installed ncurses DSO uses unsigned (four-byte) NCURSES_BOOL.
 * Parse its unchanged development header with that ABI, then restore the
 * application's C11 bool macro. Legacy Rogue opts into its own bool profile. */
#include <stdbool.h>
#pragma push_macro("bool")
#include <waste/ncurses/curses.h>
#pragma pop_macro("bool")
#endif
