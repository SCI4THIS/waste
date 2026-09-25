#ifndef WASTE_WCTYPE_H
#define WASTE_WCTYPE_H

#include <wchar.h>

typedef unsigned int wctype_t;

wctype_t wctype(const char *);
int iswctype(wint_t, wctype_t);
int iswalnum(wint_t);
int iswalpha(wint_t);
int iswblank(wint_t);
int iswcntrl(wint_t);
int iswdigit(wint_t);
int iswlower(wint_t);
int iswprint(wint_t);
int iswspace(wint_t);
int iswupper(wint_t);
int towlower(wint_t);
int towupper(wint_t);

#endif
