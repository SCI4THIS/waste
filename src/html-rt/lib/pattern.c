/* pattern.c — Glob pattern matching and basic regular expressions for the
 * WASTE guest libc. */

#include "common.h"

static i32 glob_match(const char*p,const char*s,i32 insensitive){while(*p){if(*p=='*'){while(*p=='*')p++;if(!*p)return 1;do{if(glob_match(p,s,insensitive))return 1;}while(*s++);return 0;}if(!*s)return 0;if(*p=='?'){p++;s++;continue;}i32 a=(unsigned char)*p++,b=(unsigned char)*s++;if(insensitive){a=lower_ascii(a);b=lower_ascii(b);}if(a!=b)return 0;}return!*s;}
i32 fnmatch(const char*pattern,const char*text,i32 flags){return glob_match(pattern,text,flags&16)?0:1;}

typedef struct WasteRegex{const char*pattern;i32 flags;}WasteRegex;
i32 regcomp(WasteRegex*regex,const char*pattern,i32 flags){regex->pattern=pattern;regex->flags=flags;return 0;}
static i32 regex_here(const char*p,const char*s,i32 icase){if(!*p||(*p=='$'&&!p[1]))return !*p||!*s;if(p[1]=='*'){do{if(regex_here(p+2,s,icase))return 1;}while(*s&&(*p=='.'||(icase?lower_ascii(*p)==lower_ascii(*s):*p==*s))&&s++);return 0;}if(*s&&(*p=='.'||(icase?lower_ascii(*p)==lower_ascii(*s):*p==*s)))return regex_here(p+1,s+1,icase);return 0;}
i32 regexec(const WasteRegex*regex,const char*text,u32 matches,void*match,i32 flags){(void)matches;(void)match;(void)flags;const char*p=regex->pattern;i32 icase=regex->flags&2;if(*p=='^')return regex_here(p+1,text,icase)?0:1;do{if(regex_here(p,text,icase))return 0;}while(*text++);return 1;}
void regfree(WasteRegex*regex){regex->pattern=0;regex->flags=0;}
