/* termcap.c — VT100/ANSI termcap for the WASTE guest libc.
 *
 * Provides the escape sequences that readline (and other termcap consumers)
 * need for interactive line editing.  The capability set matches the VT/ANSI
 * subset handled by the WasteTerminalModel in the browser frontend. */

#include "include/helper.h"

/* --- String capabilities (tgetstr) --- */

typedef struct { char id[4]; const char *value; } tc_string;

static const tc_string string_caps[] = {
    /* Cursor movement */
    {"le", "\b"},               /* cursor left                         */
    {"nd", "\033[C"},           /* cursor right (non-destructive space) */
    {"up", "\033[A"},           /* cursor up                           */
    {"do", "\n"},               /* cursor down                         */
    {"cr", "\r"},               /* carriage return                     */
    {"cm", "\033[%i%d;%dH"},    /* cursor motion (row;col)             */

    /* Erase */
    {"ce", "\033[K"},           /* clear to end of line                */
    {"cd", "\033[J"},           /* clear to end of display             */
    {"cl", "\033[H\033[2J"},    /* clear screen and home               */

    /* Attributes */
    {"so", "\033[7m"},          /* standout begin (inverse)            */
    {"se", "\033[27m"},         /* standout end                        */
    {"us", "\033[4m"},          /* underline begin                     */
    {"ue", "\033[24m"},         /* underline end                       */
    {"md", "\033[1m"},          /* bold begin                          */
    {"me", "\033[0m"},          /* all attributes off                  */

    /* Cursor visibility */
    {"ve", "\033[?25h"},        /* cursor visible (normal)             */
    {"vi", "\033[?25l"},        /* cursor invisible                    */
    {"vs", "\033[?25h"},        /* cursor very visible                 */

    /* Scroll */
    {"sf", "\n"},               /* scroll forward                      */

    /* Insert/exit insert mode — VT100 has no separate insert mode;
       readline falls back to overwrite+redraw when these are empty. */
    {"im", ""},
    {"ei", ""},

    /* Keypad */
    {"ks", ""},                 /* keypad transmit start               */
    {"ke", ""},                 /* keypad transmit end                 */

    /* Arrow key strings */
    {"ku", "\033[A"},           /* key up                              */
    {"kd", "\033[B"},           /* key down                            */
    {"kr", "\033[C"},           /* key right                           */
    {"kl", "\033[D"},           /* key left                            */

    /* Miscellaneous */
    {"bl", "\a"},               /* audible bell                        */
    {"pc", ""},                 /* padding character                   */

    {"",   (const char *)0}     /* sentinel                            */
};

/* --- Entry point --- */

i32 tgetent(char *buffer, const char *terminal) {
    (void)buffer; (void)terminal;
    return 1;   /* always succeed — we provide built-in VT100 capabilities */
}

/* --- Boolean capabilities --- */

i32 tgetflag(const char *id) {
    if (!id) return 0;
    if (id[0] == 'a' && id[1] == 'm') return 1;  /* auto margins          */
    return 0;
}

/* --- Numeric capabilities --- */

i32 tgetnum(const char *id) {
    if (!id) return -1;
    if (id[0] == 'c' && id[1] == 'o') return 80;  /* columns              */
    if (id[0] == 'l' && id[1] == 'i') return 24;  /* lines                */
    return -1;
}

/* --- String capabilities --- */

char *tgetstr(const char *id, char **area) {
    if (!id) return (char *)0;
    for (const tc_string *e = string_caps; e->value; e++) {
        if (e->id[0] == id[0] && e->id[1] == id[1]) {
            if (area && *area) {
                char *dest = *area;
                const char *src = e->value;
                while (*src) *dest++ = *src++;
                *dest = '\0';
                char *result = *area;
                *area = dest + 1;
                return result;
            }
            return (char *)e->value;
        }
    }
    return (char *)0;
}

/* --- Cursor addressing --- */

char *tgoto(const char *cap, i32 col, i32 row) {
    static char buf[64];
    if (!cap) { buf[0] = '\0'; return buf; }

    char *out = buf;
    char *end = buf + sizeof(buf) - 1;
    i32 params[2] = {row, col};
    i32 pi = 0;
    i32 incr = 0;

    for (const char *p = cap; *p && out < end; p++) {
        if (*p != '%') { *out++ = *p; continue; }
        p++;
        if (!*p) break;
        switch (*p) {
        case 'd': {
            i32 v = (pi < 2 ? params[pi++] : 0) + incr;
            if (v < 0) v = 0;
            if (v >= 100 && out + 3 <= end) {
                *out++ = (char)('0' + v / 100);
                *out++ = (char)('0' + (v / 10) % 10);
                *out++ = (char)('0' + v % 10);
            } else if (v >= 10 && out + 2 <= end) {
                *out++ = (char)('0' + v / 10);
                *out++ = (char)('0' + v % 10);
            } else {
                *out++ = (char)('0' + v % 10);
            }
            break;
        }
        case 'i': incr = 1; break;
        case 'r': { i32 t = params[0]; params[0] = params[1]; params[1] = t; break; }
        case '%': *out++ = '%'; break;
        default: break;
        }
    }
    *out = '\0';
    return buf;
}

/* --- Output with padding --- */

i32 tputs(const char *text, i32 lines, i32 (*put)(i32)) {
    (void)lines;
    if (!text) return 0;
    while (*text) {
        /* Skip termcap padding specifications ($<NNN>) — the browser
           terminal has no hardware delay. */
        if (*text == '$' && *(text + 1) == '<') {
            text += 2;
            while (*text && *text != '>') text++;
            if (*text == '>') text++;
            continue;
        }
        if (put((unsigned char)*text++) < 0) return -1;
    }
    return 0;
}
