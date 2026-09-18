/* termcap.c — Termcap stubs for the WASTE guest libc. */

#include "include/helper.h"

i32 tgetent(char*buffer,const char*terminal){(void)buffer;(void)terminal;return 1;}
i32 tgetflag(const char*id){(void)id;return 0;} i32 tgetnum(const char*id){(void)id;return-1;}
char *tgetstr(const char*id,char**area){(void)id;(void)area;return 0;}
char *tgoto(const char*capability,i32 column,i32 row){(void)column;(void)row;return(char*)capability;}
i32 tputs(const char*text,i32 lines,i32(*put)(i32)){(void)lines;while(*text)if(put((unsigned char)*text++)<0)return-1;return 0;}
