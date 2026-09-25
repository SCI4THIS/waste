#ifndef WASTE_WCHAR_H
#define WASTE_WCHAR_H

#include <stddef.h>
#include <uchar.h>

#ifndef WCHAR_MIN
# define WCHAR_MIN (-2147483647 - 1)
# define WCHAR_MAX 2147483647
#endif
#define WEOF ((wint_t)-1)

size_t wcslen(const wchar_t *);
wchar_t *wmemchr(const wchar_t *, wchar_t, size_t);
wchar_t *wmempcpy(wchar_t *, const wchar_t *, size_t);
wchar_t *wcscat(wchar_t *, const wchar_t *);
int wcwidth(wchar_t);
int wcswidth(const wchar_t *, size_t);
int iswalpha(wint_t);
int iswalnum(wint_t);
int iswblank(wint_t);
int iswcntrl(wint_t);
int iswdigit(wint_t);
int iswlower(wint_t);
int iswprint(wint_t);
int iswgraph(wint_t);
int iswpunct(wint_t);
int iswxdigit(wint_t);
int iswspace(wint_t);
int iswupper(wint_t);
int towlower(wint_t);
int towupper(wint_t);
size_t mbrtowc(wchar_t *, const char *, size_t, mbstate_t *);
int mbtowc(wchar_t *, const char *, size_t);
int mblen(const char *, size_t);
size_t mbsrtowcs(wchar_t *, const char **, size_t, mbstate_t *);
size_t wcrtomb(char *, wchar_t, mbstate_t *);
wint_t btowc(int);
int wctob(wint_t);

#endif
