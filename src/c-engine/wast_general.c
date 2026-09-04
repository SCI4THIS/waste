/*
 * wast_general.c — Self-contained WAT/WAST parser + stack-machine interpreter.
 * Handles bulk-operations.wast: memory.init/copy/fill, table.init/copy/fill,
 * call_indirect, ref.func, i32.load8_u, local.get, i32.const, call.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef WASTE_FREESTANDING
void *malloc(size_t);
void *calloc(size_t, size_t);
void  free(void *);
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
int   memcmp(const void *, const void *, size_t);
int   snprintf(char *, size_t, const char *, ...);
int   strcmp(const char *, const char *);
size_t strlen(const char *);
#else
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#endif

#include "wast_general.h"
#include "wast_types.h"

/* =========================================================
 * Internal IR types
 * ========================================================= */

#define GM_MAX_TYPES     16
#define GM_MAX_FUNCS     32
#define GM_MAX_PARAMS     8
#define GM_MAX_INSTRS   128
#define GM_MAX_DATA       8
#define GM_MAX_ELEM       8
#define GM_MAX_DATA_LEN  64
#define GM_MAX_REFS      32
#define GM_ID_LEN        64
#define GM_NAME_LEN     128
#define GM_STACK_LEN     64

typedef enum {
    GI_I32_CONST = 0,
    GI_LOCAL_GET,
    GI_CALL,
    GI_CALL_INDIRECT,
    GI_I32_LOAD8_U,
    GI_REF_FUNC,
    GI_MEMORY_INIT,
    GI_MEMORY_COPY,
    GI_MEMORY_FILL,
    GI_TABLE_INIT,
    GI_TABLE_COPY,
    GI_TABLE_FILL,
    GI_I32_OP,      /* i32 arithmetic/comparison/bitwise; Wasm opcode in imm0 */
} GInstrOp;

typedef struct {
    GInstrOp  op;
    int32_t   iconst;  /* I32_CONST value */
    uint32_t  imm0;    /* local/func/type/seg index */
    uint32_t  imm1;    /* secondary (e.g. table idx for call_indirect) */
} GInstr;

typedef struct {
    int  param_count;
    int  result_count;
    char id[GM_ID_LEN];
} GType;

typedef struct {
    int      type_idx;
    int      param_count;
    int      result_count;
    GInstr   code[GM_MAX_INSTRS];
    int      code_len;
    char     id[GM_ID_LEN];
    char     export_name[GM_NAME_LEN];
    /* local param names for lookup during parse */
    char     param_ids[GM_MAX_PARAMS][GM_ID_LEN];
} GFunc;

typedef struct {
    uint8_t bytes[GM_MAX_DATA_LEN];
    int     len;
    char    id[GM_ID_LEN];
} GData;

typedef struct {
    uint32_t refs[GM_MAX_REFS];
    int      count;
    char     id[GM_ID_LEN];
} GElem;

typedef struct {
    GType  types[GM_MAX_TYPES];
    int    type_count;
    GFunc  funcs[GM_MAX_FUNCS];
    int    func_count;
    GData  data[GM_MAX_DATA];
    int    data_count;
    GElem  elem[GM_MAX_ELEM];
    int    elem_count;
    int    has_memory;
    uint32_t mem_pages;
    int    has_table;
    uint32_t table_size;
} GModule;

/* Execution value: i32 or funcref */
#define GVAL_I32  0
#define GVAL_REF  1
#define GREF_NULL UINT32_MAX

typedef struct {
    int      tag;
    int32_t  i32;
    uint32_t ref;
} GVal;

/* Live instance */
typedef struct {
    GModule   mod;
    uint8_t  *mem;
    uint32_t  mem_bytes;
    uint32_t *table;      /* func indices; UINT32_MAX = null ref */
    uint32_t  table_size;
    char      error[256];
} GInst;

/* =========================================================
 * Tokenizer
 * ========================================================= */

typedef enum {
    T_EOF = 0, T_LPAREN, T_RPAREN, T_KW, T_ID, T_NAT, T_INT, T_STR
} TKind;

typedef struct {
    TKind       kind;
    const char *ptr;
    int         len;
    int64_t     ival;
    char        sbuf[GM_MAX_DATA_LEN];
    int         slen;
} Tok;

typedef struct {
    const char *src;
    int pos, len;
    Tok  peek;
    int  has_peek;
    char err[256];
} Lex;

static void lex_init(Lex *L, const char *src, int len) {
    L->src = src; L->pos = 0; L->len = len;
    L->has_peek = 0; L->err[0] = '\0';
}

static int is_idc(char c) {
    return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||
           c=='!'||c=='#'||c=='$'||c=='%'||c=='&'||c=='\''||c=='*'||
           c=='+'||c=='-'||c=='.'||c=='/'||c==':'||c=='<'||c=='='||
           c=='>'||c=='?'||c=='@'||c=='\\'||c=='^'||c=='_'||c=='`'||
           c=='|'||c=='~';
}

static void skip_ws(Lex *L) {
    while (L->pos < L->len) {
        char c = L->src[L->pos];
        if (c==' '||c=='\t'||c=='\r'||c=='\n') { L->pos++; continue; }
        if (c==';'&&L->pos+1<L->len&&L->src[L->pos+1]==';') {
            while (L->pos<L->len&&L->src[L->pos]!='\n') L->pos++;
            continue;
        }
        if (c=='('&&L->pos+1<L->len&&L->src[L->pos+1]==';') {
            L->pos+=2; int d=1;
            while (L->pos<L->len&&d>0) {
                if (L->src[L->pos]=='('&&L->pos+1<L->len&&L->src[L->pos+1]==';')
                    { d++; L->pos+=2; }
                else if (L->src[L->pos]==';'&&L->pos+1<L->len&&L->src[L->pos+1]==')')
                    { d--; L->pos+=2; }
                else L->pos++;
            }
            continue;
        }
        break;
    }
}

static Tok lex_read(Lex *L) {
    Tok t; memset(&t,0,sizeof(t));
    if (L->has_peek) { L->has_peek=0; return L->peek; }
    skip_ws(L);
    if (L->pos>=L->len) { t.kind=T_EOF; return t; }
    char c=L->src[L->pos]; t.ptr=L->src+L->pos;
    if (c=='(') { t.kind=T_LPAREN; t.len=1; L->pos++; return t; }
    if (c==')') { t.kind=T_RPAREN; t.len=1; L->pos++; return t; }
    if (c=='$') {
        t.kind=T_ID; int s=L->pos++;
        while (L->pos<L->len&&is_idc(L->src[L->pos])) L->pos++;
        t.ptr=L->src+s; t.len=L->pos-s; return t;
    }
    if (c=='"') {
        t.kind=T_STR; int s=L->pos++; t.slen=0;
        while (L->pos<L->len&&L->src[L->pos]!='"') {
            char sc=L->src[L->pos];
            if (sc=='\\'&&L->pos+1<L->len) {
                L->pos++;
                char ec=L->src[L->pos];
                if (ec=='n') { if(t.slen<GM_MAX_DATA_LEN) t.sbuf[t.slen++]='\n'; }
                else if (ec=='t') { if(t.slen<GM_MAX_DATA_LEN) t.sbuf[t.slen++]='\t'; }
                else if (ec=='"') { if(t.slen<GM_MAX_DATA_LEN) t.sbuf[t.slen++]='"'; }
                else if (ec=='\\') { if(t.slen<GM_MAX_DATA_LEN) t.sbuf[t.slen++]='\\'; }
                else {
                    int h1=0,h2=0;
                    if(ec>='0'&&ec<='9') h1=ec-'0';
                    else if(ec>='a'&&ec<='f') h1=ec-'a'+10;
                    else if(ec>='A'&&ec<='F') h1=ec-'A'+10;
                    if(L->pos+1<L->len) {
                        char ec2=L->src[L->pos+1];
                        if((ec2>='0'&&ec2<='9')||(ec2>='a'&&ec2<='f')||(ec2>='A'&&ec2<='F')) {
                            L->pos++;
                            if(ec2>='0'&&ec2<='9') h2=ec2-'0';
                            else if(ec2>='a'&&ec2<='f') h2=ec2-'a'+10;
                            else h2=ec2-'A'+10;
                        }
                    }
                    if(t.slen<GM_MAX_DATA_LEN) t.sbuf[t.slen++]=(char)((h1<<4)|h2);
                }
            } else { if(t.slen<GM_MAX_DATA_LEN) t.sbuf[t.slen++]=sc; }
            L->pos++;
        }
        if (L->pos<L->len) L->pos++;
        t.ptr=L->src+s; t.len=L->pos-s; return t;
    }
    if ((c>='0'&&c<='9')||c=='+'||c=='-') {
        int s=L->pos; int neg=0;
        if(c=='+'||c=='-') { if(c=='-') neg=1; L->pos++; }
        uint64_t val=0;
        if(L->pos+1<L->len&&L->src[L->pos]=='0'&&
           (L->src[L->pos+1]=='x'||L->src[L->pos+1]=='X')) {
            L->pos+=2;
            while(L->pos<L->len) {
                char hc=L->src[L->pos];
                if(hc=='_'){L->pos++;continue;}
                if(hc>='0'&&hc<='9') val=val*16+(hc-'0');
                else if(hc>='a'&&hc<='f') val=val*16+(hc-'a'+10);
                else if(hc>='A'&&hc<='F') val=val*16+(hc-'A'+10);
                else break;
                L->pos++;
            }
        } else {
            while(L->pos<L->len) {
                char dc=L->src[L->pos];
                if(dc=='_'){L->pos++;continue;}
                if(dc<'0'||dc>'9') break;
                val=val*10+(dc-'0'); L->pos++;
            }
        }
        t.ptr=L->src+s; t.len=L->pos-s;
        t.kind=neg?T_INT:T_NAT; t.ival=neg?-(int64_t)val:(int64_t)val;
        return t;
    }
    if (is_idc(c)) {
        t.kind=T_KW; int s=L->pos;
        while(L->pos<L->len&&is_idc(L->src[L->pos])) L->pos++;
        t.ptr=L->src+s; t.len=L->pos-s; return t;
    }
    L->pos++; t.kind=T_EOF; return t;
}

static Tok lex_peek(Lex *L) {
    if (!L->has_peek) { L->peek=lex_read(L); L->has_peek=1; }
    return L->peek;
}

static int kw(const Tok *t, const char *s) {
    int n=0; while(s[n]) n++;
    return t->len==n && memcmp(t->ptr,s,n)==0;
}

static void copy_id(char *dst, const Tok *t, int maxlen) {
    int n = t->len < maxlen-1 ? t->len : maxlen-1;
    memcpy(dst, t->ptr, n); dst[n]='\0';
}

/* Skip a complete s-expression starting after '(' has been consumed.
   Nesting depth is already 1. */
static void skip_sexp(Lex *L) {
    int depth=1;
    while (depth>0) {
        Tok t=lex_read(L);
        if(t.kind==T_EOF) break;
        if(t.kind==T_LPAREN) depth++;
        if(t.kind==T_RPAREN) depth--;
    }
}

/* =========================================================
 * Parser helpers
 * ========================================================= */

static int find_type_by_id(const GModule *m, const char *id) {
    for(int i=0;i<m->type_count;i++)
        if(strcmp(m->types[i].id,id)==0) return i;
    return -1;
}

static int find_func_by_id(const GModule *m, const char *id) {
    for(int i=0;i<m->func_count;i++)
        if(strcmp(m->funcs[i].id,id)==0) return i;
    return -1;
}

static int find_data_by_id(const GModule *m, const char *id) {
    for(int i=0;i<m->data_count;i++)
        if(strcmp(m->data[i].id,id)==0) return i;
    return -1;
}

static int find_elem_by_id(const GModule *m, const char *id) {
    for(int i=0;i<m->elem_count;i++)
        if(strcmp(m->elem[i].id,id)==0) return i;
    return -1;
}

static int find_param_by_id(const GFunc *f, const char *id) {
    for(int i=0;i<f->param_count;i++)
        if(strcmp(f->param_ids[i],id)==0) return i;
    return -1;
}

/* Resolve a function reference: either $id or nat index */
static int resolve_func(const GModule *m, const Tok *t) {
    if (t->kind==T_ID) {
        char id[GM_ID_LEN]; copy_id(id,t,GM_ID_LEN);
        return find_func_by_id(m,id);
    }
    if (t->kind==T_NAT||t->kind==T_INT) return (int)t->ival;
    return -1;
}

/* =========================================================
 * Instruction parser (folded form)
 * Emits into f->code[]. Returns 0 on success, -1 on error.
 * Called after '(' and opcode keyword have been consumed.
 * Parses immediates then sub-expressions, emits instructions.
 * ========================================================= */
static int parse_instr(Lex *L, GModule *m, GFunc *f, const char *opname, int oplen, char *err);

/* Parse zero or more instructions until ')' */
static int parse_instrs_until_rparen(Lex *L, GModule *m, GFunc *f, char *err) {
    for (;;) {
        Tok p=lex_peek(L);
        if (p.kind==T_RPAREN||p.kind==T_EOF) return 0;
        if (p.kind==T_LPAREN) {
            lex_read(L); /* consume '(' */
            Tok op=lex_read(L);
            if (op.kind!=T_KW&&op.kind!=T_ID) {
                snprintf(err,256,"expected opcode"); return -1;
            }
            if (parse_instr(L,m,f,op.ptr,op.len,err)<0) return -1;
            /* closing ')' already consumed by parse_instr */
        } else if (p.kind==T_KW) {
            /* bare (non-folded) instruction — not needed for bulk-ops */
            lex_read(L);
            /* skip until we can't handle it */
            snprintf(err,256,"bare instructions not supported: %.*s",p.len,p.ptr);
            return -1;
        } else {
            lex_read(L); /* skip unexpected */
        }
    }
}

static int emit(GFunc *f, GInstr instr, char *err) {
    if (f->code_len>=GM_MAX_INSTRS) { snprintf(err,256,"too many instrs"); return -1; }
    f->code[f->code_len++]=instr;
    return 0;
}

static int parse_instr(Lex *L, GModule *m, GFunc *f, const char *opname, int oplen, char *err) {
    GInstr ins; memset(&ins,0,sizeof(ins));

#define IS_OP(s) (oplen==(int)sizeof(s)-1 && memcmp(opname,s,oplen)==0)

    if (IS_OP("i32.const")) {
        Tok v=lex_read(L);
        if(v.kind!=T_NAT&&v.kind!=T_INT) { snprintf(err,256,"expected i32"); return -1; }
        ins.op=GI_I32_CONST; ins.iconst=(int32_t)v.ival;
        if(emit(f,ins,err)<0) return -1;
    }
    else if (IS_OP("local.get")) {
        Tok v=lex_read(L);
        int idx=-1;
        if(v.kind==T_ID) {
            char id[GM_ID_LEN]; copy_id(id,&v,GM_ID_LEN);
            idx=find_param_by_id(f,id);
            if(idx<0) { snprintf(err,256,"unknown local: %s",id); return -1; }
        } else if(v.kind==T_NAT||v.kind==T_INT) {
            idx=(int)v.ival;
        } else { snprintf(err,256,"expected local idx"); return -1; }
        ins.op=GI_LOCAL_GET; ins.imm0=(uint32_t)idx;
        if(emit(f,ins,err)<0) return -1;
    }
    else if (IS_OP("call")) {
        Tok v=lex_read(L);
        int idx=resolve_func(m,&v);
        if(idx<0) { snprintf(err,256,"unknown func"); return -1; }
        ins.op=GI_CALL; ins.imm0=(uint32_t)idx;
        if(parse_instrs_until_rparen(L,m,f,err)<0) return -1;
        if(emit(f,ins,err)<0) return -1;
    }
    else if (IS_OP("call_indirect")) {
        /* (call_indirect (type $T) sub-exprs...) */
        uint32_t type_idx=0;
        Tok p=lex_peek(L);
        if(p.kind==T_LPAREN) {
            lex_read(L);
            Tok kk=lex_read(L);
            if(kw(&kk,"type")) {
                Tok tid=lex_read(L);
                if(tid.kind==T_ID) {
                    char id[GM_ID_LEN]; copy_id(id,&tid,GM_ID_LEN);
                    int ti=find_type_by_id(m,id);
                    if(ti>=0) type_idx=(uint32_t)ti;
                } else if(tid.kind==T_NAT||tid.kind==T_INT) {
                    type_idx=(uint32_t)tid.ival;
                }
                /* consume ')' of (type ...) */
                Tok rp=lex_read(L);
                if(rp.kind!=T_RPAREN) { snprintf(err,256,"expected )"); return -1; }
            } else {
                /* not (type...), treat as sub-expr — put back? We can't, so handle */
                snprintf(err,256,"expected (type ...) in call_indirect"); return -1;
            }
        }
        ins.op=GI_CALL_INDIRECT; ins.imm0=type_idx; ins.imm1=0/*table 0*/;
        /* parse operand sub-expressions */
        if(parse_instrs_until_rparen(L,m,f,err)<0) return -1;
        if(emit(f,ins,err)<0) return -1;
    }
    else if (IS_OP("i32.load8_u")) {
        ins.op=GI_I32_LOAD8_U;
        /* optional (offset=N align=N) immediates — not used in bulk-ops */
        /* sub-expressions */
        if(parse_instrs_until_rparen(L,m,f,err)<0) return -1;
        if(emit(f,ins,err)<0) return -1;
    }
    else if (IS_OP("ref.func")) {
        Tok v=lex_read(L);
        int idx=resolve_func(m,&v);
        if(idx<0) { snprintf(err,256,"unknown func for ref.func"); return -1; }
        ins.op=GI_REF_FUNC; ins.imm0=(uint32_t)idx;
        if(emit(f,ins,err)<0) return -1;
    }
    else if (IS_OP("memory.init")) {
        Tok v=lex_read(L);
        int seg=-1;
        if(v.kind==T_ID) {
            char id[GM_ID_LEN]; copy_id(id,&v,GM_ID_LEN);
            seg=find_data_by_id(m,id);
            if(seg<0) { snprintf(err,256,"unknown data segment: %s",id); return -1; }
        } else if(v.kind==T_NAT||v.kind==T_INT) { seg=(int)v.ival; }
        else { snprintf(err,256,"expected data seg"); return -1; }
        ins.op=GI_MEMORY_INIT; ins.imm0=(uint32_t)seg;
        if(parse_instrs_until_rparen(L,m,f,err)<0) return -1;
        if(emit(f,ins,err)<0) return -1;
    }
    else if (IS_OP("memory.copy")) {
        ins.op=GI_MEMORY_COPY;
        if(parse_instrs_until_rparen(L,m,f,err)<0) return -1;
        if(emit(f,ins,err)<0) return -1;
    }
    else if (IS_OP("memory.fill")) {
        ins.op=GI_MEMORY_FILL;
        if(parse_instrs_until_rparen(L,m,f,err)<0) return -1;
        if(emit(f,ins,err)<0) return -1;
    }
    else if (IS_OP("table.init")) {
        Tok v=lex_read(L);
        int seg=-1;
        if(v.kind==T_ID) {
            char id[GM_ID_LEN]; copy_id(id,&v,GM_ID_LEN);
            seg=find_elem_by_id(m,id);
            if(seg<0) { snprintf(err,256,"unknown elem segment: %s",id); return -1; }
        } else if(v.kind==T_NAT||v.kind==T_INT) { seg=(int)v.ival; }
        else { snprintf(err,256,"expected elem seg"); return -1; }
        ins.op=GI_TABLE_INIT; ins.imm0=(uint32_t)seg;
        if(parse_instrs_until_rparen(L,m,f,err)<0) return -1;
        if(emit(f,ins,err)<0) return -1;
    }
    else if (IS_OP("table.copy")) {
        ins.op=GI_TABLE_COPY;
        if(parse_instrs_until_rparen(L,m,f,err)<0) return -1;
        if(emit(f,ins,err)<0) return -1;
    }
    else if (IS_OP("table.fill")) {
        ins.op=GI_TABLE_FILL;
        if(parse_instrs_until_rparen(L,m,f,err)<0) return -1;
        if(emit(f,ins,err)<0) return -1;
    }
    else {
        /* Try i32 arithmetic/comparison/bitwise/unary ops */
        static const struct { const char *nm; uint32_t op; } i32_tbl[] = {
            {"i32.eqz",0x45},{"i32.eq",0x46},{"i32.ne",0x47},
            {"i32.lt_s",0x48},{"i32.lt_u",0x49},
            {"i32.gt_s",0x4a},{"i32.gt_u",0x4b},
            {"i32.le_s",0x4c},{"i32.le_u",0x4d},
            {"i32.ge_s",0x4e},{"i32.ge_u",0x4f},
            {"i32.clz",0x67},{"i32.ctz",0x68},{"i32.popcnt",0x69},
            {"i32.add",0x6a},{"i32.sub",0x6b},{"i32.mul",0x6c},
            {"i32.div_s",0x6d},{"i32.div_u",0x6e},
            {"i32.rem_s",0x6f},{"i32.rem_u",0x70},
            {"i32.and",0x71},{"i32.or",0x72},{"i32.xor",0x73},
            {"i32.shl",0x74},{"i32.shr_s",0x75},{"i32.shr_u",0x76},
            {"i32.rotl",0x77},{"i32.rotr",0x78},
            {"i32.extend8_s",0xc0},{"i32.extend16_s",0xc1},
            {NULL,0}
        };
        uint32_t found_op=0;
        for(int _t=0;i32_tbl[_t].nm;_t++) {
            size_t _n=strlen(i32_tbl[_t].nm);
            if((size_t)oplen==_n&&memcmp(opname,i32_tbl[_t].nm,_n)==0) {
                found_op=i32_tbl[_t].op; break;
            }
        }
        if(found_op) {
            ins.op=GI_I32_OP; ins.imm0=found_op;
            if(parse_instrs_until_rparen(L,m,f,err)<0) return -1;
            if(emit(f,ins,err)<0) return -1;
        } else {
            /* Unknown — skip the rest of this s-expression */
            skip_sexp(L);
            return 0; /* already consumed ')' inside skip_sexp */
        }
    }

    /* Consume the closing ')' of this folded instruction */
    Tok rp=lex_read(L);
    if(rp.kind!=T_RPAREN) { snprintf(err,256,"expected closing ) for %.*s",oplen,opname); return -1; }
    return 0;
#undef IS_OP
}

/* =========================================================
 * Module parser
 * ========================================================= */

/* Parse (func ...) content after '(' 'func' have been consumed.
   The func is appended to m->funcs[]. */
static int parse_func(Lex *L, GModule *m, char *err) {
    if(m->func_count>=GM_MAX_FUNCS) { snprintf(err,256,"too many funcs"); return -1; }
    GFunc *f=&m->funcs[m->func_count];
    memset(f,0,sizeof(*f));

    /* Optional $id */
    Tok p=lex_peek(L);
    if(p.kind==T_ID) { lex_read(L); copy_id(f->id,&p,GM_ID_LEN); p=lex_peek(L); }

    /* Attributes: (export "name"), (type $T), (param ...), (result ...) */
    while(p.kind==T_LPAREN) {
        lex_read(L); /* consume '(' */
        Tok kk=lex_read(L);
        if(kw(&kk,"export")) {
            Tok s=lex_read(L);
            if(s.kind==T_STR) {
                int n=s.slen<GM_NAME_LEN-1?s.slen:GM_NAME_LEN-1;
                memcpy(f->export_name,s.sbuf,n); f->export_name[n]='\0';
            }
            lex_read(L); /* ')' */
        } else if(kw(&kk,"type")) {
            Tok tid=lex_read(L);
            if(tid.kind==T_ID) {
                char id[GM_ID_LEN]; copy_id(id,&tid,GM_ID_LEN);
                int ti=find_type_by_id(m,id);
                if(ti>=0) {
                    f->type_idx=ti;
                    f->param_count=m->types[ti].param_count;
                    f->result_count=m->types[ti].result_count;
                }
            } else if(tid.kind==T_NAT||tid.kind==T_INT) {
                int ti=(int)tid.ival;
                if(ti>=0&&ti<m->type_count) {
                    f->type_idx=ti;
                    f->param_count=m->types[ti].param_count;
                    f->result_count=m->types[ti].result_count;
                }
            }
            lex_read(L); /* ')' */
        } else if(kw(&kk,"param")) {
            /* (param $id? type ...) */
            for(;;) {
                Tok pp=lex_peek(L);
                if(pp.kind==T_RPAREN) { lex_read(L); break; }
                if(pp.kind==T_ID) {
                    /* named param */
                    lex_read(L);
                    if(f->param_count<GM_MAX_PARAMS)
                        copy_id(f->param_ids[f->param_count],&pp,GM_ID_LEN);
                    Tok ty=lex_read(L); /* type name, skip value */
                    (void)ty;
                    f->param_count++;
                } else if(pp.kind==T_KW) {
                    /* unnamed param type */
                    lex_read(L);
                    if(f->param_count<GM_MAX_PARAMS) f->param_ids[f->param_count][0]='\0';
                    f->param_count++;
                } else { lex_read(L); break; }
            }
            f->result_count=0; /* may be set by (result) */
        } else if(kw(&kk,"result")) {
            /* skip result types */
            f->result_count=0;
            for(;;) {
                Tok rr=lex_peek(L);
                if(rr.kind==T_RPAREN) { lex_read(L); break; }
                if(rr.kind==T_KW) { lex_read(L); f->result_count++; }
                else { lex_read(L); break; }
            }
        } else if(kw(&kk,"local")) {
            /* skip local declarations */
            for(;;) {
                Tok ll=lex_peek(L);
                if(ll.kind==T_RPAREN||ll.kind==T_EOF) break;
                lex_read(L);
            }
            lex_read(L); /* ')' */
        } else {
            /* Unknown attribute — could be inline instruction in func body.
               Put it back by treating it as opcode. */
            if(parse_instr(L,m,f,kk.ptr,kk.len,err)<0) return -1;
            /* parse more instructions */
            if(parse_instrs_until_rparen(L,m,f,err)<0) return -1;
            goto done_func;
        }
        p=lex_peek(L);
    }

    /* Body: instructions until ')' */
    if(parse_instrs_until_rparen(L,m,f,err)<0) return -1;

done_func:
    /* Consume closing ')' of (func ...) */
    {
        Tok rp=lex_read(L);
        if(rp.kind!=T_RPAREN) { snprintf(err,256,"expected ) after func body"); return -1; }
    }
    m->func_count++;
    return 0;
}

/* Parse the contents of (module ...) after '(' 'module' consumed. */
static int parse_module(Lex *L, GModule *m, char *err) {
    memset(m,0,sizeof(*m));
    for(;;) {
        Tok p=lex_peek(L);
        if(p.kind==T_RPAREN||p.kind==T_EOF) { lex_read(L); return 0; }
        if(p.kind!=T_LPAREN) { lex_read(L); continue; }
        lex_read(L); /* '(' */
        Tok kk=lex_read(L);

        if(kw(&kk,"type")) {
            if(m->type_count>=GM_MAX_TYPES) { skip_sexp(L); continue; }
            GType *ty=&m->types[m->type_count];
            memset(ty,0,sizeof(*ty));
            /* Optional $id */
            Tok p2=lex_peek(L);
            if(p2.kind==T_ID) { lex_read(L); copy_id(ty->id,&p2,GM_ID_LEN); }
            /* (func (param ...)* (result ...)?) */
            for(;;) {
                Tok pp=lex_peek(L);
                if(pp.kind==T_RPAREN||pp.kind==T_EOF) break;
                if(pp.kind==T_LPAREN) {
                    lex_read(L);
                    Tok kk2=lex_read(L);
                    if(kw(&kk2,"func")) {
                        for(;;) {
                            Tok fp=lex_peek(L);
                            if(fp.kind==T_RPAREN||fp.kind==T_EOF) break;
                            if(fp.kind==T_LPAREN) {
                                lex_read(L);
                                Tok fkk=lex_read(L);
                                if(kw(&fkk,"param")) {
                                    int cnt=0;
                                    for(;;) {
                                        Tok ap=lex_peek(L);
                                        if(ap.kind==T_RPAREN||ap.kind==T_EOF) break;
                                        if(ap.kind==T_KW||ap.kind==T_ID) { lex_read(L); if(ap.kind==T_KW) cnt++; }
                                        else break;
                                    }
                                    ty->param_count+=cnt;
                                    lex_read(L); /* ')' */
                                } else if(kw(&fkk,"result")) {
                                    int cnt=0;
                                    for(;;) {
                                        Tok ap=lex_peek(L);
                                        if(ap.kind==T_RPAREN||ap.kind==T_EOF) break;
                                        if(ap.kind==T_KW) { lex_read(L); cnt++; }
                                        else break;
                                    }
                                    ty->result_count+=cnt;
                                    lex_read(L); /* ')' */
                                } else { skip_sexp(L); }
                            } else { lex_read(L); }
                        }
                        lex_read(L); /* ')' of (func) */
                    } else { skip_sexp(L); }
                } else { lex_read(L); }
            }
            lex_read(L); /* ')' of (type) */
            m->type_count++;
        }
        else if(kw(&kk,"memory")) {
            m->has_memory=1;
            Tok v=lex_peek(L);
            if(v.kind==T_NAT||v.kind==T_INT) { lex_read(L); m->mem_pages=(uint32_t)v.ival; }
            /* skip rest */
            for(;;) { Tok rr=lex_peek(L); if(rr.kind==T_RPAREN||rr.kind==T_EOF) break; lex_read(L); }
            lex_read(L); /* ')' */
        }
        else if(kw(&kk,"table")) {
            m->has_table=1;
            Tok v=lex_peek(L);
            if(v.kind==T_NAT||v.kind==T_INT) { lex_read(L); m->table_size=(uint32_t)v.ival; }
            for(;;) { Tok rr=lex_peek(L); if(rr.kind==T_RPAREN||rr.kind==T_EOF) break; lex_read(L); }
            lex_read(L); /* ')' */
        }
        else if(kw(&kk,"data")) {
            if(m->data_count>=GM_MAX_DATA) { skip_sexp(L); continue; }
            GData *d=&m->data[m->data_count];
            memset(d,0,sizeof(*d));
            /* Optional $id */
            Tok p2=lex_peek(L);
            if(p2.kind==T_ID) { lex_read(L); copy_id(d->id,&p2,GM_ID_LEN); p2=lex_peek(L); }
            /* String content */
            if(p2.kind==T_STR) {
                lex_read(L);
                int n=p2.slen<GM_MAX_DATA_LEN?p2.slen:GM_MAX_DATA_LEN;
                memcpy(d->bytes,p2.sbuf,n); d->len=n;
            }
            /* skip rest */
            for(;;) { Tok rr=lex_peek(L); if(rr.kind==T_RPAREN||rr.kind==T_EOF) break; lex_read(L); }
            lex_read(L); /* ')' */
            m->data_count++;
        }
        else if(kw(&kk,"elem")) {
            if(m->elem_count>=GM_MAX_ELEM) { skip_sexp(L); continue; }
            GElem *e=&m->elem[m->elem_count];
            memset(e,0,sizeof(*e));
            /* Optional $id */
            Tok p2=lex_peek(L);
            if(p2.kind==T_ID) { lex_read(L); copy_id(e->id,&p2,GM_ID_LEN); p2=lex_peek(L); }
            /* 'func' keyword */
            if(p2.kind==T_KW&&kw(&p2,"func")) { lex_read(L); p2=lex_peek(L); }
            /* function references */
            while(p2.kind==T_ID||p2.kind==T_NAT) {
                lex_read(L);
                int idx=-1;
                if(p2.kind==T_ID) {
                    char id[GM_ID_LEN]; copy_id(id,&p2,GM_ID_LEN);
                    idx=find_func_by_id(m,id);
                }
                else idx=(int)p2.ival;
                if(idx>=0&&e->count<GM_MAX_REFS) e->refs[e->count++]=(uint32_t)idx;
                p2=lex_peek(L);
            }
            for(;;) { Tok rr=lex_peek(L); if(rr.kind==T_RPAREN||rr.kind==T_EOF) break; lex_read(L); }
            lex_read(L); /* ')' */
            m->elem_count++;
        }
        else if(kw(&kk,"func")) {
            if(parse_func(L,m,err)<0) return -1;
            /* parse_func already consumed its closing ')' */
        }
        else {
            /* Unknown field — skip the rest of this s-expression.
               We're inside '(' field_name ..., so skip_sexp finishes it. */
            skip_sexp(L);
        }
    }
}

/* =========================================================
 * Instantiation
 * ========================================================= */

static int ginst_init(GInst *inst, const GModule *m, char *err) {
    memset(inst,0,sizeof(*inst));
    inst->mod=*m;

    if(m->has_memory) {
        inst->mem_bytes=m->mem_pages*65536u;
        if(inst->mem_bytes==0) inst->mem_bytes=65536u;
        inst->mem=(uint8_t*)calloc(1,inst->mem_bytes);
        if(!inst->mem) { snprintf(err,256,"OOM: memory"); return -1; }
    }
    if(m->has_table) {
        inst->table_size=m->table_size;
        inst->table=(uint32_t*)malloc(inst->table_size*sizeof(uint32_t));
        if(!inst->table) { free(inst->mem); snprintf(err,256,"OOM: table"); return -1; }
        for(uint32_t i=0;i<inst->table_size;i++) inst->table[i]=GREF_NULL;
    }
    return 0;
}

static void ginst_free(GInst *inst) {
    free(inst->mem); inst->mem=NULL;
    free(inst->table); inst->table=NULL;
}

/* =========================================================
 * Executor
 * ========================================================= */

static int exec_gfunc(GInst *inst, uint32_t func_idx,
                       GVal *args, int nargs,
                       GVal *out_result, int *out_nresults,
                       char *err);

static int exec_gfunc(GInst *inst, uint32_t func_idx,
                       GVal *args, int nargs,
                       GVal *out_result, int *out_nresults,
                       char *err)
{
    if(func_idx>=(uint32_t)inst->mod.func_count) {
        snprintf(err,256,"func index %u out of range",func_idx); return -1;
    }
    const GFunc *f=&inst->mod.funcs[func_idx];

    /* locals = params + zero-initialized extras (none for bulk-ops) */
    GVal locals[GM_MAX_PARAMS];
    memset(locals,0,sizeof(locals));
    int nlocals=nargs<GM_MAX_PARAMS?nargs:GM_MAX_PARAMS;
    for(int i=0;i<nlocals;i++) locals[i]=args[i];

    /* operand stack */
    GVal stk[GM_STACK_LEN];
    int top=0;

#define PUSH(v) do { if(top>=GM_STACK_LEN){snprintf(err,256,"stack overflow");return -1;} stk[top++]=(v); } while(0)
#define POP(v)  do { if(top<=0){snprintf(err,256,"stack underflow");return -1;} (v)=stk[--top]; } while(0)

    for(int pc=0;pc<f->code_len;pc++) {
        const GInstr *ins=&f->code[pc];
        switch(ins->op) {
        case GI_I32_CONST: {
            GVal v; v.tag=GVAL_I32; v.i32=ins->iconst; v.ref=0; PUSH(v);
            break;
        }
        case GI_LOCAL_GET: {
            uint32_t idx=ins->imm0;
            if(idx>=(uint32_t)f->param_count) { snprintf(err,256,"local.get OOB %u",idx); return -1; }
            PUSH(locals[idx]);
            break;
        }
        case GI_CALL: {
            /* All args are on stack; we need to know how many.
               For simplicity, look at target func's param_count. */
            uint32_t fi=ins->imm0;
            if(fi>=(uint32_t)inst->mod.func_count) { snprintf(err,256,"call OOB"); return -1; }
            const GFunc *tf=&inst->mod.funcs[fi];
            int na=tf->param_count;
            if(top<na) { snprintf(err,256,"not enough args for call"); return -1; }
            GVal cargs[GM_MAX_PARAMS]; int ci;
            for(ci=na-1;ci>=0;ci--) { POP(cargs[ci]); }
            GVal cres; int cnres=0;
            if(exec_gfunc(inst,fi,cargs,na,&cres,&cnres,err)<0) return -1;
            if(cnres>0) PUSH(cres);
            break;
        }
        case GI_CALL_INDIRECT: {
            /* Stack: ... args... table_idx
               type_idx in imm0, table in imm1 (=0) */
            GVal idx_val; POP(idx_val);
            uint32_t tidx=(uint32_t)idx_val.i32;
            if(tidx>=inst->table_size) { snprintf(err,256,"call_indirect OOB table"); return -1; }
            uint32_t fi=inst->table[tidx];
            if(fi==GREF_NULL) { snprintf(err,256,"call_indirect null ref"); return -1; }
            if(fi>=(uint32_t)inst->mod.func_count) { snprintf(err,256,"call_indirect fi OOB"); return -1; }
            const GFunc *tf=&inst->mod.funcs[fi];
            int na=tf->param_count;
            if(top<na) { snprintf(err,256,"not enough args for call_indirect"); return -1; }
            GVal cargs[GM_MAX_PARAMS]; int ci;
            for(ci=na-1;ci>=0;ci--) { POP(cargs[ci]); }
            GVal cres; int cnres=0;
            if(exec_gfunc(inst,fi,cargs,na,&cres,&cnres,err)<0) return -1;
            if(cnres>0) PUSH(cres);
            break;
        }
        case GI_I32_LOAD8_U: {
            GVal addr_val; POP(addr_val);
            uint32_t addr=(uint32_t)addr_val.i32;
            if(!inst->mem||addr>=inst->mem_bytes) { snprintf(err,256,"i32.load8_u OOB"); return -1; }
            GVal v; v.tag=GVAL_I32; v.i32=(int32_t)inst->mem[addr]; v.ref=0; PUSH(v);
            break;
        }
        case GI_REF_FUNC: {
            GVal v; v.tag=GVAL_REF; v.ref=ins->imm0; v.i32=0; PUSH(v);
            break;
        }
        case GI_MEMORY_INIT: {
            /* Stack: d, s, n */
            GVal nv,sv,dv; POP(nv); POP(sv); POP(dv);
            uint32_t d=(uint32_t)dv.i32, s=(uint32_t)sv.i32, n=(uint32_t)nv.i32;
            uint32_t seg=ins->imm0;
            if(seg>=(uint32_t)inst->mod.data_count) { snprintf(err,256,"memory.init bad seg"); return -1; }
            const GData *ds=&inst->mod.data[seg];
            if(s+n>(uint32_t)ds->len||d+n>inst->mem_bytes) { snprintf(err,256,"memory.init OOB"); return -1; }
            memcpy(inst->mem+d, ds->bytes+s, n);
            break;
        }
        case GI_MEMORY_COPY: {
            /* Stack: d, s, n (memmove semantics) */
            GVal nv,sv,dv; POP(nv); POP(sv); POP(dv);
            uint32_t d=(uint32_t)dv.i32, s=(uint32_t)sv.i32, n=(uint32_t)nv.i32;
            if(d+n>inst->mem_bytes||s+n>inst->mem_bytes) { snprintf(err,256,"memory.copy OOB"); return -1; }
            if(n==0) break;
            if(d<=s||s+n<=d) {
                /* no overlap, or dst before src */
                memcpy(inst->mem+d, inst->mem+s, n);
            } else {
                /* overlap: dst > src, copy backward */
                for(uint32_t i=n;i>0;i--) inst->mem[d+i-1]=inst->mem[s+i-1];
            }
            break;
        }
        case GI_MEMORY_FILL: {
            /* Stack: d, val, n */
            GVal nv,vv,dv; POP(nv); POP(vv); POP(dv);
            uint32_t d=(uint32_t)dv.i32, val=(uint32_t)vv.i32&0xFF, n=(uint32_t)nv.i32;
            if(d+n>inst->mem_bytes) { snprintf(err,256,"memory.fill OOB"); return -1; }
            memset(inst->mem+d, (int)val, n);
            break;
        }
        case GI_TABLE_INIT: {
            /* Stack: d, s, n */
            GVal nv,sv,dv; POP(nv); POP(sv); POP(dv);
            uint32_t d=(uint32_t)dv.i32, s=(uint32_t)sv.i32, n=(uint32_t)nv.i32;
            uint32_t seg=ins->imm0;
            if(seg>=(uint32_t)inst->mod.elem_count) { snprintf(err,256,"table.init bad seg"); return -1; }
            const GElem *es=&inst->mod.elem[seg];
            if(s+n>(uint32_t)es->count||d+n>inst->table_size) { snprintf(err,256,"table.init OOB"); return -1; }
            for(uint32_t i=0;i<n;i++) inst->table[d+i]=es->refs[s+i];
            break;
        }
        case GI_TABLE_COPY: {
            /* Stack: d, s, n */
            GVal nv,sv,dv; POP(nv); POP(sv); POP(dv);
            uint32_t d=(uint32_t)dv.i32, s=(uint32_t)sv.i32, n=(uint32_t)nv.i32;
            if(d+n>inst->table_size||s+n>inst->table_size) { snprintf(err,256,"table.copy OOB"); return -1; }
            if(n==0) break;
            if(d<=s||s+n<=d) {
                for(uint32_t i=0;i<n;i++) inst->table[d+i]=inst->table[s+i];
            } else {
                for(uint32_t i=n;i>0;i--) inst->table[d+i-1]=inst->table[s+i-1];
            }
            break;
        }
        case GI_TABLE_FILL: {
            /* Stack: d, val (ref), n */
            GVal nv,vv,dv; POP(nv); POP(vv); POP(dv);
            uint32_t d=(uint32_t)dv.i32, n=(uint32_t)nv.i32;
            uint32_t ref=(vv.tag==GVAL_REF)?vv.ref:GREF_NULL;
            if(d+n>inst->table_size) { snprintf(err,256,"table.fill OOB"); return -1; }
            for(uint32_t i=0;i<n;i++) inst->table[d+i]=ref;
            break;
        }
        case GI_I32_OP: {
            uint32_t op=ins->imm0;
            int is_unary=(op==0x45||op==0x67||op==0x68||op==0x69||op==0xc0||op==0xc1);
            GVal rval,lval; uint32_t ub,ua=0,result=0; int32_t b,a=0;
            POP(rval); b=rval.i32; ub=(uint32_t)b;
            if(!is_unary){ POP(lval); a=lval.i32; ua=(uint32_t)a; }
            switch(op){
                case 0x45: result=(ub==0u); break;
                case 0x46: result=(ua==ub); break;
                case 0x47: result=(ua!=ub); break;
                case 0x48: result=((int32_t)ua<(int32_t)ub); break;
                case 0x49: result=(ua<ub); break;
                case 0x4a: result=((int32_t)ua>(int32_t)ub); break;
                case 0x4b: result=(ua>ub); break;
                case 0x4c: result=((int32_t)ua<=(int32_t)ub); break;
                case 0x4d: result=(ua<=ub); break;
                case 0x4e: result=((int32_t)ua>=(int32_t)ub); break;
                case 0x4f: result=(ua>=ub); break;
                case 0x67: result=ub?(uint32_t)__builtin_clz(ub):32u; break;
                case 0x68: result=ub?(uint32_t)__builtin_ctz(ub):32u; break;
                case 0x69: result=(uint32_t)__builtin_popcount(ub); break;
                case 0x6a: result=ua+ub; break;
                case 0x6b: result=ua-ub; break;
                case 0x6c: result=ua*ub; break;
                case 0x6d:
                    if(!ub){ snprintf(err,256,"integer divide by zero"); return -1; }
                    if(ua==0x80000000u&&ub==0xFFFFFFFFu){ snprintf(err,256,"integer overflow"); return -1; }
                    result=(uint32_t)((int32_t)ua/(int32_t)ub); break;
                case 0x6e:
                    if(!ub){ snprintf(err,256,"integer divide by zero"); return -1; }
                    result=ua/ub; break;
                case 0x6f:
                    if(!ub){ snprintf(err,256,"integer divide by zero"); return -1; }
                    result=(ua==0x80000000u&&ub==0xFFFFFFFFu)?0u:(uint32_t)((int32_t)ua%(int32_t)ub); break;
                case 0x70:
                    if(!ub){ snprintf(err,256,"integer divide by zero"); return -1; }
                    result=ua%ub; break;
                case 0x71: result=ua&ub; break;
                case 0x72: result=ua|ub; break;
                case 0x73: result=ua^ub; break;
                case 0x74: result=ua<<(ub&31u); break;
                case 0x75: result=(uint32_t)(a>>(b&31)); break;
                case 0x76: result=ua>>(ub&31u); break;
                case 0x77: { uint32_t cnt=ub&31u; result=cnt?(ua<<cnt)|(ua>>(32u-cnt)):ua; break; }
                case 0x78: { uint32_t cnt=ub&31u; result=cnt?(ua>>cnt)|(ua<<(32u-cnt)):ua; break; }
                case 0xc0: result=(uint32_t)(int32_t)(int8_t)(uint8_t)ub; break;
                case 0xc1: result=(uint32_t)(int32_t)(int16_t)(uint16_t)ub; break;
                default: snprintf(err,256,"unknown i32 op 0x%x",op); return -1;
            }
            GVal rv2; rv2.tag=GVAL_I32; rv2.i32=(int32_t)result; rv2.ref=0;
            PUSH(rv2);
            break;
        }
        default:
            snprintf(err,256,"unknown opcode %d",(int)ins->op); return -1;
        }
    }

    if(out_nresults) {
        *out_nresults = (f->result_count>0&&top>0) ? 1 : 0;
        if(*out_nresults&&out_result) *out_result=stk[top-1];
    }
    return 0;
#undef PUSH
#undef POP
}

/* Find export by name, return func index or -1 */
static int find_export(const GInst *inst, const char *name) {
    for(int i=0;i<inst->mod.func_count;i++) {
        if(strcmp(inst->mod.funcs[i].export_name,name)==0) return i;
    }
    return -1;
}

/* Invoke exported function */
static int ginvoke(GInst *inst, const char *name,
                    GVal *args, int nargs,
                    GVal *result, int *nresults,
                    char *err)
{
    int fi=find_export(inst,name);
    if(fi<0) { snprintf(err,256,"export not found: %s",name); return -1; }
    return exec_gfunc(inst,(uint32_t)fi,args,nargs,result,nresults,err);
}

/* =========================================================
 * JSON output helpers (no-alloc)
 * ========================================================= */
#ifndef WASTE_FREESTANDING
static void json_str(const char *s) {
    putchar('"');
    for(;*s;s++) {
        if(*s=='"') fputs("\\\"",stdout);
        else if(*s=='\\') fputs("\\\\",stdout);
        else if(*s=='\n') fputs("\\n",stdout);
        else putchar(*s);
    }
    putchar('"');
}
#endif

/* =========================================================
 * WAST script value parser
 * ========================================================= */

/* Parse (i32.const N) → GVal */
static int parse_val(Lex *L, GVal *out, char *err) {
    Tok lp=lex_read(L);
    if(lp.kind!=T_LPAREN) { snprintf(err,256,"expected ( before value"); return -1; }
    Tok ty=lex_read(L);
    Tok n=lex_read(L);
    GVal v; memset(&v,0,sizeof(v));
    if(kw(&ty,"i32.const")) {
        v.tag=GVAL_I32;
        v.i32=(n.kind==T_INT||n.kind==T_NAT)?(int32_t)n.ival:0;
    } else {
        /* unsupported value type — skip */
        v.tag=GVAL_I32; v.i32=0;
        while(n.kind!=T_RPAREN&&n.kind!=T_EOF) n=lex_read(L);
        *out=v; return 0;
    }
    Tok rp=lex_read(L);
    if(rp.kind!=T_RPAREN) { snprintf(err,256,"expected ) in value"); return -1; }
    *out=v; return 0;
}

/* =========================================================
 * WAST script runner
 * ========================================================= */

#ifndef WASTE_FREESTANDING
int wast_general_run(const char *path) {
    /* Read file */
    FILE *f=fopen(path,"rb");
    if(!f) { fprintf(stderr,"cannot open %s\n",path); return 1; }
    fseek(f,0,SEEK_END); long fsz=ftell(f); fseek(f,0,SEEK_SET);
    if(fsz<0) { fclose(f); fprintf(stderr,"cannot stat %s\n",path); return 1; }
    char *src=(char*)malloc((size_t)fsz+2);
    if(!src) { fclose(f); fprintf(stderr,"OOM\n"); return 1; }
    size_t nr=fread(src,1,(size_t)fsz,f); fclose(f);
    src[nr]='\0'; src[nr+1]='\0';

    /* Extract basename */
    const char *base=path;
    for(const char *p=path;*p;p++) if(*p=='/'||*p=='\\') base=p+1;

    Lex L; lex_init(&L,src,(int)nr);
    GModule mod; GInst inst; memset(&inst,0,sizeof(inst));
    int has_inst=0;
    char perr[256]={0};

    int total=0, passed=0, first=1;

    printf("{\"file\":"); json_str(base); printf(",\"assertions\":[\n");

    for(;;) {
        Tok p=lex_peek(&L);
        if(p.kind==T_EOF) break;
        if(p.kind!=T_LPAREN) { lex_read(&L); continue; }
        lex_read(&L); /* '(' */
        Tok kk=lex_read(&L);

        if(kw(&kk,"module")) {
            if(has_inst) { ginst_free(&inst); has_inst=0; }
            perr[0]='\0';
            if(parse_module(&L,&mod,perr)<0) {
                fprintf(stderr,"parse error: %s\n",perr); continue;
            }
            perr[0]='\0';
            if(ginst_init(&inst,&mod,perr)<0) {
                fprintf(stderr,"init error: %s\n",perr); continue;
            }
            has_inst=1;
        }
        else if(kw(&kk,"invoke")) {
            /* Fire-and-forget invoke */
            if(!has_inst) { skip_sexp(&L); continue; }
            Tok nm=lex_read(&L);
            if(nm.kind!=T_STR) { skip_sexp(&L); continue; }
            char name[GM_NAME_LEN];
            int nlen=nm.slen<GM_NAME_LEN-1?nm.slen:GM_NAME_LEN-1;
            memcpy(name,nm.sbuf,nlen); name[nlen]='\0';
            /* Parse args */
            GVal args[GM_MAX_PARAMS]; int nargs=0;
            for(;;) {
                Tok ap=lex_peek(&L);
                if(ap.kind==T_RPAREN||ap.kind==T_EOF) break;
                if(ap.kind==T_LPAREN) {
                    if(nargs<GM_MAX_PARAMS) {
                        if(parse_val(&L,&args[nargs],perr)>=0) nargs++;
                        else break;
                    } else break;
                } else { lex_read(&L); }
            }
            GVal res; int nres=0;
            perr[0]='\0';
            ginvoke(&inst,name,args,nargs,&res,&nres,perr);
            /* consume ')' */
            Tok rp=lex_read(&L);
            if(rp.kind!=T_RPAREN) { /* skip */ }
        }
        else if(kw(&kk,"assert_return")) {
            if(!has_inst) { skip_sexp(&L); total++; continue; }
            /* (assert_return (invoke "name" args...) expected...) */
            Tok lp2=lex_read(&L);
            if(lp2.kind!=T_LPAREN) { skip_sexp(&L); total++; continue; }
            Tok inv=lex_read(&L);
            if(!kw(&inv,"invoke")) { skip_sexp(&L); total++; continue; }
            Tok nm=lex_read(&L);
            if(nm.kind!=T_STR) { skip_sexp(&L); total++; continue; }
            char name[GM_NAME_LEN];
            int nlen=nm.slen<GM_NAME_LEN-1?nm.slen:GM_NAME_LEN-1;
            memcpy(name,nm.sbuf,nlen); name[nlen]='\0';
            /* Parse invoke args */
            GVal args[GM_MAX_PARAMS]; int nargs=0;
            for(;;) {
                Tok ap=lex_peek(&L);
                if(ap.kind==T_RPAREN||ap.kind==T_EOF) break;
                if(ap.kind==T_LPAREN) {
                    if(nargs<GM_MAX_PARAMS) {
                        if(parse_val(&L,&args[nargs],perr)>=0) nargs++;
                        else { lex_read(&L); }
                    } else { lex_read(&L); }
                } else lex_read(&L);
            }
            Tok close_inv=lex_read(&L); /* ')' of (invoke ...) */
            (void)close_inv;

            /* Parse expected results */
            GVal expected[4]; int nexp=0;
            for(;;) {
                Tok ep=lex_peek(&L);
                if(ep.kind==T_RPAREN||ep.kind==T_EOF) break;
                if(ep.kind==T_LPAREN) {
                    if(nexp<4) {
                        if(parse_val(&L,&expected[nexp],perr)>=0) nexp++;
                        else { lex_read(&L); break; }
                    } else { lex_read(&L); break; }
                } else lex_read(&L);
            }
            Tok close_ar=lex_read(&L); /* ')' of (assert_return ...) */
            (void)close_ar;

            /* Execute */
            GVal result; int nres=0;
            perr[0]='\0';
            int ok=0;
            if(ginvoke(&inst,name,args,nargs,&result,&nres,perr)>=0) {
                if(nexp==0) {
                    ok=1;
                } else if(nres>0&&nexp>0) {
                    if(expected[0].tag==GVAL_I32&&result.tag==GVAL_I32)
                        ok=(result.i32==expected[0].i32);
                    else if(expected[0].tag==GVAL_REF&&result.tag==GVAL_REF)
                        ok=(result.ref==expected[0].ref);
                }
            }
            if(ok) passed++;

            if(!first) printf(",\n");
            first=0;
            printf("{\"index\":%d,\"func\":",total);
            json_str(name);
            printf(",\"pass\":%s,\"error\":",ok?"true":"false");
            if(ok||perr[0]=='\0') printf("null");
            else json_str(perr);
            printf("}");
            total++;
        }
        else if(kw(&kk,"assert_trap")) {
            if(!has_inst) { skip_sexp(&L); total++; continue; }
            /* (assert_trap (invoke "name" args...) "message") */
            Tok lp2=lex_read(&L);
            if(lp2.kind!=T_LPAREN) { skip_sexp(&L); total++; continue; }
            Tok inv=lex_read(&L);
            if(!kw(&inv,"invoke")) { skip_sexp(&L); total++; continue; }
            Tok nm=lex_read(&L);
            if(nm.kind!=T_STR) { skip_sexp(&L); total++; continue; }
            char name[GM_NAME_LEN];
            int nlen=nm.slen<GM_NAME_LEN-1?nm.slen:GM_NAME_LEN-1;
            memcpy(name,nm.sbuf,nlen); name[nlen]='\0';
            /* Parse invoke args */
            GVal args[GM_MAX_PARAMS]; int nargs=0;
            for(;;) {
                Tok ap=lex_peek(&L);
                if(ap.kind==T_RPAREN||ap.kind==T_EOF) break;
                if(ap.kind==T_LPAREN) {
                    if(nargs<GM_MAX_PARAMS) {
                        if(parse_val(&L,&args[nargs],perr)>=0) nargs++;
                        else break;
                    } else break;
                } else { lex_read(&L); }
            }
            Tok close_inv=lex_read(&L); /* ')' of (invoke ...) */
            (void)close_inv;
            /* Skip expected message string */
            Tok msg=lex_peek(&L);
            if(msg.kind==T_STR) lex_read(&L);
            Tok close_at=lex_read(&L); /* ')' of (assert_trap ...) */
            (void)close_at;

            /* Execute — pass if it traps (returns -1) */
            GVal result; int nres=0;
            perr[0]='\0';
            int trapped=(ginvoke(&inst,name,args,nargs,&result,&nres,perr)<0);
            int ok=trapped;
            if(ok) passed++;

            if(!first) printf(",\n");
            first=0;
            printf("{\"index\":%d,\"func\":",total);
            json_str(name);
            printf(",\"pass\":%s,\"error\":",ok?"true":"false");
            if(ok) printf("null");
            else {
                /* didn't trap — report what we got */
                const char *msg2="expected trap but returned normally";
                printf("\""); printf("%s",msg2); printf("\"");
            }
            printf("}");
            total++;
        }
        else {
            skip_sexp(&L);
        }
    }

    printf("\n],\"passed\":%d,\"total\":%d}\n",passed,total);
    free(src);
    if(has_inst) ginst_free(&inst);
    return (passed==total)?0:1;
}
#endif /* !WASTE_FREESTANDING */

/* =========================================================
 * Browser / freestanding API
 * ========================================================= */

static GInst g_gen_inst;
static int   g_gen_loaded = 0;
static char  g_gen_error[256];

uint32_t waste_gen_load_module(uint32_t text_ptr, uint32_t text_len) {
    if(g_gen_loaded) { ginst_free(&g_gen_inst); g_gen_loaded=0; }
    g_gen_error[0]='\0';
    const char *src=(const char*)(uintptr_t)text_ptr;
    Lex L; lex_init(&L,src,(int)text_len);
    GModule mod;
    if(parse_module(&L,&mod,g_gen_error)<0) return 1;
    if(ginst_init(&g_gen_inst,&mod,g_gen_error)<0) return 1;
    g_gen_loaded=1;
    return 0;
}

uint32_t waste_gen_invoke(uint32_t name_ptr, uint32_t name_len,
                          uint32_t args_ptr, uint32_t arg_count) {
    if(!g_gen_loaded) {
        memcpy(g_gen_error,"no module",9); g_gen_error[9]='\0'; return 1;
    }
    char name[GM_NAME_LEN];
    uint32_t nl=name_len<GM_NAME_LEN-1?name_len:(GM_NAME_LEN-1);
    memcpy(name,(const void*)(uintptr_t)name_ptr,nl); name[nl]='\0';

    /* flat-value args: 33 bytes each [type, 16 data, 16 nan_mode] */
    GVal args[GM_MAX_PARAMS]; int nargs=0;
    const uint8_t *flat=(const uint8_t*)(uintptr_t)args_ptr;
    for(uint32_t i=0;i<arg_count&&nargs<GM_MAX_PARAMS;i++,nargs++) {
        const uint8_t *fv=flat+i*33;
        args[nargs].tag=GVAL_I32;
        int32_t v; memcpy(&v,fv+1,4); args[nargs].i32=v;
    }
    GVal res; int nres=0;
    g_gen_error[0]='\0';
    if(ginvoke(&g_gen_inst,name,args,nargs,&res,&nres,g_gen_error)<0) return 1;
    return 0;
}

uint32_t waste_gen_assert_return(uint32_t name_ptr, uint32_t name_len,
                                  uint32_t args_ptr, uint32_t arg_count,
                                  uint32_t alts_ptr, uint32_t alt_count,
                                  uint32_t result_count) {
    if(!g_gen_loaded) {
        const char *m="no module";
        memcpy(g_gen_error,m,9); g_gen_error[9]='\0'; return 0;
    }
    char name[GM_NAME_LEN];
    uint32_t nl=name_len<GM_NAME_LEN-1?name_len:(GM_NAME_LEN-1);
    memcpy(name,(const void*)(uintptr_t)name_ptr,nl); name[nl]='\0';

    GVal args[GM_MAX_PARAMS]; int nargs=0;
    const uint8_t *flat=(const uint8_t*)(uintptr_t)args_ptr;
    for(uint32_t i=0;i<arg_count&&nargs<GM_MAX_PARAMS;i++,nargs++) {
        const uint8_t *fv=flat+i*33;
        args[nargs].tag=GVAL_I32;
        int32_t v; memcpy(&v,fv+1,4); args[nargs].i32=v;
    }

    GVal res; int nres=0;
    g_gen_error[0]='\0';
    if(ginvoke(&g_gen_inst,name,args,nargs,&res,&nres,g_gen_error)<0) return 0;

    if(alt_count==0||result_count==0) return 1;
    if(nres<1) {
        const char *m="no result"; memcpy(g_gen_error,m,9); g_gen_error[9]='\0'; return 0;
    }

    /* Check each alternative (each is result_count * 33 bytes) */
    const uint8_t *alts=(const uint8_t*)(uintptr_t)alts_ptr;
    uint32_t stride=result_count*33u;
    for(uint32_t a=0;a<alt_count;a++) {
        const uint8_t *alt=alts+a*stride;
        /* Only check first result for now */
        uint8_t exp_type=alt[0];
        if(exp_type==0/*i32*/) {
            int32_t ev; memcpy(&ev,alt+1,4);
            if(res.tag==GVAL_I32&&res.i32==ev) return 1;
        }
    }
    {
        const char *pf="result mismatch: ";
        int pos=0;
        while(*pf&&pos<200) g_gen_error[pos++]=*pf++;
        for(int k=0;name[k]&&pos<254;k++) g_gen_error[pos++]=name[k];
        g_gen_error[pos]='\0';
    }
    return 0;
}

uint32_t waste_gen_error_ptr(void) {
    return (uint32_t)(uintptr_t)g_gen_error;
}
