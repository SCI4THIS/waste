#ifndef WASTE_ROGUE_IMPORTS_H
#define WASTE_ROGUE_IMPORTS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <termios.h>
#include <curses.h>
#include <unctrl.h>

/* Rogue uses these libc exports, which the mounted public stdio header does
 * not yet declare. Keep compatibility declarations local to this client. */
extern char *fgets(char *, int, FILE *);
extern int vsprintf(char *, const char *, va_list);

/* Keep the public SDK types and assign library namespaces at compilation.
 * Kernel adapters and data relocation globals retain their runtime bindings.
 * CRT stream accessors are declared by waste-crt.c itself. */
#define WASTE_ROGUE_IMPORT(library, name) \
    extern __typeof__(name) name __attribute__((import_module(library)))

WASTE_ROGUE_IMPORT("libc", __errno_location);
WASTE_ROGUE_IMPORT("libc", abs);
WASTE_ROGUE_IMPORT("libc", atoi);
WASTE_ROGUE_IMPORT("libc", calloc);
WASTE_ROGUE_IMPORT("libc", fclose);
WASTE_ROGUE_IMPORT("libc", fflush);
WASTE_ROGUE_IMPORT("libc", fgets);
WASTE_ROGUE_IMPORT("libc", fopen);
WASTE_ROGUE_IMPORT("libc", fprintf);
WASTE_ROGUE_IMPORT("libc", fread);
WASTE_ROGUE_IMPORT("libc", free);
WASTE_ROGUE_IMPORT("libc", getenv);
WASTE_ROGUE_IMPORT("libc", localtime);
WASTE_ROGUE_IMPORT("libc", lstat);
WASTE_ROGUE_IMPORT("libc", malloc);
WASTE_ROGUE_IMPORT("libc", memset);
WASTE_ROGUE_IMPORT("libc", perror);
WASTE_ROGUE_IMPORT("libc", printf);
WASTE_ROGUE_IMPORT("libc", putc);
WASTE_ROGUE_IMPORT("libc", putchar);
WASTE_ROGUE_IMPORT("libc", rewind);
WASTE_ROGUE_IMPORT("libc", setbuf);
WASTE_ROGUE_IMPORT("libc", signal);
WASTE_ROGUE_IMPORT("libc", sprintf);
WASTE_ROGUE_IMPORT("libc", srand);
WASTE_ROGUE_IMPORT("libc", sscanf);
WASTE_ROGUE_IMPORT("libc", stat);
WASTE_ROGUE_IMPORT("libc", strcat);
WASTE_ROGUE_IMPORT("libc", strchr);
WASTE_ROGUE_IMPORT("libc", strcmp);
WASTE_ROGUE_IMPORT("libc", strcpy);
WASTE_ROGUE_IMPORT("libc", strerror);
WASTE_ROGUE_IMPORT("libc", strlen);
WASTE_ROGUE_IMPORT("libc", strncat);
WASTE_ROGUE_IMPORT("libc", strncmp);
WASTE_ROGUE_IMPORT("libc", strncpy);
WASTE_ROGUE_IMPORT("libc", tcgetattr);
WASTE_ROGUE_IMPORT("libc", time);
WASTE_ROGUE_IMPORT("libc", toascii);
WASTE_ROGUE_IMPORT("libc", vsprintf);

WASTE_ROGUE_IMPORT("libncurses", baudrate);
WASTE_ROGUE_IMPORT("libncurses", clearok);
WASTE_ROGUE_IMPORT("libncurses", curs_set);
WASTE_ROGUE_IMPORT("libncurses", delwin);
WASTE_ROGUE_IMPORT("libncurses", endwin);
WASTE_ROGUE_IMPORT("libncurses", erasechar);
WASTE_ROGUE_IMPORT("libncurses", flushinp);
WASTE_ROGUE_IMPORT("libncurses", halfdelay);
WASTE_ROGUE_IMPORT("libncurses", idlok);
WASTE_ROGUE_IMPORT("libncurses", initscr);
WASTE_ROGUE_IMPORT("libncurses", isendwin);
WASTE_ROGUE_IMPORT("libncurses", keypad);
WASTE_ROGUE_IMPORT("libncurses", killchar);
WASTE_ROGUE_IMPORT("libncurses", leaveok);
WASTE_ROGUE_IMPORT("libncurses", mvcur);
WASTE_ROGUE_IMPORT("libncurses", mvprintw);
WASTE_ROGUE_IMPORT("libncurses", mvwin);
WASTE_ROGUE_IMPORT("libncurses", mvwprintw);
WASTE_ROGUE_IMPORT("libncurses", newwin);
WASTE_ROGUE_IMPORT("libncurses", nocbreak);
WASTE_ROGUE_IMPORT("libncurses", noecho);
WASTE_ROGUE_IMPORT("libncurses", printw);
WASTE_ROGUE_IMPORT("libncurses", raw);
WASTE_ROGUE_IMPORT("libncurses", subwin);
WASTE_ROGUE_IMPORT("libncurses", unctrl);
WASTE_ROGUE_IMPORT("libncurses", waddch);
WASTE_ROGUE_IMPORT("libncurses", waddnstr);
WASTE_ROGUE_IMPORT("libncurses", wattrset);
WASTE_ROGUE_IMPORT("libncurses", wclear);
WASTE_ROGUE_IMPORT("libncurses", wclrtoeol);
WASTE_ROGUE_IMPORT("libncurses", werase);
WASTE_ROGUE_IMPORT("libncurses", wgetch);
WASTE_ROGUE_IMPORT("libncurses", wgetnstr);
WASTE_ROGUE_IMPORT("libncurses", winch);
WASTE_ROGUE_IMPORT("libncurses", wmove);
WASTE_ROGUE_IMPORT("libncurses", wprintw);
WASTE_ROGUE_IMPORT("libncurses", wrefresh);
WASTE_ROGUE_IMPORT("libncurses", wtouchln);

#undef WASTE_ROGUE_IMPORT

#endif
