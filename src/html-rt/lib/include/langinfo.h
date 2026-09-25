#ifndef WASTE_LANGINFO_H
#define WASTE_LANGINFO_H

typedef int nl_item;

#define CODESET 1
#define D_T_FMT 2
#define D_FMT 3
#define T_FMT 4
#define RADIXCHAR 5
#define THOUSEP 6
#define YESEXPR 7
#define NOEXPR 8

char *nl_langinfo(nl_item);

#endif
