#ifndef WASTE_WCTYPE_H
#define WASTE_WCTYPE_H

#include <waste/abi/availability.h>

#include <wchar.h>

typedef unsigned int wctype_t;

wctype_t wctype(const char *);
int iswctype(wint_t, wctype_t);
int iswalnum(wint_t);
int iswalpha(wint_t) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int iswblank(wint_t) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int iswcntrl(wint_t) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int iswdigit(wint_t) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int iswlower(wint_t);
int iswprint(wint_t);
int iswspace(wint_t) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int iswupper(wint_t);
int towlower(wint_t);
int towupper(wint_t);

#endif
