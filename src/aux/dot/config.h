#ifndef WASTE_DOT_CONFIG_H
#define WASTE_DOT_CONFIG_H
#include "graphviz_version.h"
#include <unistd.h>
#define DEFAULT_DPI 96
#define BROWSER "false"
#define GVLIBDIR "/usr/lib/graphviz"
#define HAVE_GETOPT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SETENV 1
#define HAVE_STRCASESTR 1
/* Plugins are linked into dot; no host library loader is used. */
#define DEMAND_LOADING 0
#define NO_FPERR 1
#endif
