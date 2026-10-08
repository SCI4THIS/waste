/* config.h — WASTE Rogue profile for wasm32 cross-compilation. */
#ifndef ROGUE_CONFIG_H
#define ROGUE_CONFIG_H

#define HAVE_SYS_TYPES_H 1
#define HAVE_STRING_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_FCNTL_H 1
#define HAVE_MEMSET 1
#define HAVE_STRCHR 1
#define HAVE_STRERROR 1
#define HAVE_VPRINTF 1
#define HAVE_UNISTD_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_TERMIOS_H 1
#define HAVE_TERM_H 1
#define HAVE_SETENV 1

/* ncurses functions available via the shared library. */
#define HAVE_ERASECHAR 1
#define HAVE_KILLCHAR 1
#define HAVE_ESCDELAY 1
#define HAVE_NCURSES_H 1

/* Not available in the WASTE guest environment. */
/* #undef HAVE_WORKING_FORK */
/* #undef HAVE_PWD_H */
/* #undef HAVE_GETPWUID */
/* #undef HAVE_GETUID */
/* #undef HAVE_GETGID */
/* #undef HAVE_SETUID */
/* #undef HAVE_SETGID */
/* #undef HAVE_SETREUID */
/* #undef HAVE_SETREGID */
/* #undef HAVE_GETPASS */
/* #undef HAVE_ALARM */
/* #undef HAVE_GETLOADAVG */
/* #undef HAVE_ARPA_INET_H */
/* #undef HAVE_SYS_UTSNAME_H */
/* #undef HAVE_PROCESS_H */
/* #undef HAVE_SYS_IOCTL_H */

/* No scoreboard, lockfile, wizard mode, load/user limits. */
/* #undef SCOREFILE */
/* #undef LOCKFILE */
/* #undef MASTER */
/* #undef MAXLOAD */
/* #undef MAXUSERS */
/* #undef CHECKTIME */
/* #undef DUMP */

#define NUMSCORES 10
#define NUMNAME "Ten"
#define ALLSCORES 1

/* PATH_MAX is not in the guest libc limits.h. */
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define RETSIGTYPE void

#define PACKAGE_NAME "Rogue"
#define PACKAGE_VERSION "5.4.4"
#define PACKAGE_STRING "Rogue 5.4.4"
#define PACKAGE_TARNAME "rogue"

#endif /* ROGUE_CONFIG_H */
