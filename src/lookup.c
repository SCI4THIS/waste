#include <assert.h>
#include <string.h>
#include "lookup.h"
#include "dis.h"

#define LEN (len - (*buf - start_buf))

#define DEBUG_LOOKUP 1

#if DEBUG_LOOKUP
#include <stdio.h>
#define DEBUG(...) printf("%s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__)
#else
#define DEBUG(...) { }
#endif

#define NEST "  "

typedef struct functype_st   functype_t;
typedef struct import_st     import_t;
typedef struct func_st       func_t;
typedef struct tabletype_st  tabletype_t;
typedef struct memtype_st    memtype_t;
typedef struct global_st     global_t;
typedef struct export_st     export_t;
typedef struct data_st       data_t;

struct lookup_cache_st {
  struct {
    module_t     *m;
    uint32_t      n_func;
    functype_t   *functype; /* section 1 (Type) */
    import_t     *import; /* section 2 (Import) */
    func_t       *func; /* section 3 (Function) and 10 (Code) */
    tabletype_t  *table; /* section 4 (Table) */
    memtype_t    *memory; /* section 5 (Memory) */
    global_t     *global; /* section 6 (Global) */
    uint32_t      n_export;
    export_t     *export; /* section 7 (Export) */
    data_t       *data; /* section 11 (Data) */
  } h;
  uint8_t buf[];
};


typedef struct resulttype_st {
  uint32_t         n;
  const valtype_t *t;
} resulttype_t;

struct functype_st {
  struct resulttype_st rt1;
  struct resulttype_st rt2;
};

void print_wat_func_sig(lookup_cache_t **lookup, uint32_t typeidx)
{
  uint32_t    i        = 0;
  functype_t *functype = &(*lookup)->h.functype[typeidx];
  if (functype->rt1.n > 0) {
    printf(" (param");
    for (i=0; i<functype->rt1.n; i++) {
      printf(" %s", dis_valtype(functype->rt1.t[i]));
    }
    printf(")");
  }
  if (functype->rt2.n > 0) {
    printf(" (result");
    for (i=0; i<functype->rt2.n; i++) {
      printf(" %s", dis_valtype(functype->rt2.t[i]));
    }
    printf(")");
  }
}

void print_data(const uint8_t *buf, size_t len)
{
  size_t i;
  for (i=0; i<len; i++) {
    unsigned char c = buf[i];
    if (' ' <= c && c <= '~') {
      printf("%c", c);
    } else {
      printf("\\%02x", c);
    }
  }
}


size_t lookup_alloc_type_section(module_t *m, lookup_cache_t **lookup, size_t size)
{
  functype_t     *functype  = NULL;
  size_t          i         = 0;
  uint32_t        u32       = 0;
  size_t          len       = m->section[1].len;
  const uint8_t  *start_buf = m->section[1].buf;
  const uint8_t  *bufptr    = m->section[1].buf;
  const uint8_t **buf       = &bufptr;
  assert(2 * m->count.functype == m->count.resulttype);
  if (lookup) {
    leb_u32(buf, LEN, &u32);
    assert(u32 == m->count.functype);
    (*lookup)->h.functype = (functype_t*)(&((uint8_t*)*lookup)[size]);
    for (i=0; i<m->count.functype; i++) {
      (*buf)++; /* 0x60 */
      functype = (functype_t*)(&((uint8_t*)*lookup)[size]);
      leb_u32(buf, LEN, &u32);
      functype->rt1.n = u32;
      functype->rt1.t = (*buf);
      (*buf) += u32;
      leb_u32(buf, LEN, &u32);
      functype->rt2.n = u32;
      functype->rt2.t = (*buf);
      (*buf) += u32;
      size += sizeof(struct functype_st);
    }
#if DEBUG_LOOKUP
    for (i=0; i<m->count.functype; i++) {
      functype = &(*lookup)->h.functype[i];
      printf("\n" NEST "(type (;%d;) (func", i);
      print_wat_func_sig(lookup, i);
      printf("))");
    }
#endif
  } else {
    size += sizeof(struct functype_st) * m->count.functype;
  }
  return size;
}

void print_utf(uint32_t len, const uint8_t *s)
{
  uint32_t i;
  for (i=0; i<len; i++) {
    fputc(s[i], stdout);
  }
}

typedef struct name_st {
  uint32_t       len;
  const uint8_t *s; /* utf8 */
} name_t;

typedef uint32_t typeidx_t;

typedef struct limits_st {
  uint8_t  type; /* 0 = { min = n, max = e }; 1 = { min = n, max = m } */
  uint32_t n;
  uint32_t m;
} limits_t;

void lookup_name(const uint8_t **buf, size_t len, name_t *nm)
{
  uint32_t u32;
  leb_u32(buf, len, &u32);
  nm->len = u32;
  nm->s = (*buf);
  (*buf) += u32;
}

void lookup_limits(const uint8_t **buf, size_t len, limits_t *lim)
{
  uint32_t       min;
  uint32_t       max;
  const uint8_t *start_buf = *buf;
  uint8_t        type;

  type = *buf[0];
  lim->type = type;
  (*buf)++;
  switch(type) {
    case 0x00:
      leb_u32(buf, LEN, &min);
      lim->n = min;
      return;
    case 0x01:
      leb_u32(buf, LEN, &min);
      lim->n = min;
      leb_u32(buf, LEN, &max);
      lim->m = max;
      return;
    default:
      break;
  }
  assert(false);
  return;
}

typedef struct expr_st {
  const uint8_t *in;
  const uint8_t *end;
} expr_t;

void lookup_expr(const uint8_t **buf, size_t len, expr_t *e)
{
  const uint8_t *start_buf = (*buf);
  e->in = start_buf;
  while ((*buf)[0] != 0x0B) { is_valid_instr(buf, LEN, NULL); }
  (*buf)++;
  e->end = (*buf);
}

typedef uint8_t reftype_t; /* 0x70 = funcref, 0x6F = externref */

struct tabletype_st {
  reftype_t et; /* 0x70 = funcref, 0x6F = externref */
  struct limits_st lim;
};

struct memtype_st {
  struct limits_st lim;
};

typedef uint8_t mut_t; /* 0x00 = const, 0x01 = var */

typedef struct globaltype_st {
  valtype_t t;
  mut_t m;
} globaltype_t;

typedef struct importdesc_st {
  uint8_t  type; /* 0 = func, 1 = table, 2 = mem, 3 = global */
  union {
    typeidx_t            x;
    struct tabletype_st  tt;
    struct memtype_st    mt;
    struct globaltype_st gt;
  } u;
} importdesc_t;

struct import_st {
  struct name_st       mod;
  struct name_st       nm;
  struct importdesc_st d;
};


size_t lookup_alloc_import_section(module_t *m, lookup_cache_t **lookup, size_t size)
{
  size_t          i         = 0;
  uint32_t        u32       = 0;
  size_t          len       = m->section[2].len;
  const uint8_t  *start_buf = m->section[2].buf;
  const uint8_t  *bufptr    = m->section[2].buf;
  const uint8_t **buf       = &bufptr;
  import_t       *import    = NULL;
  uint8_t         type      = 0;

  if (lookup) {
    leb_u32(buf, LEN, &u32);
    assert(u32 == m->count.import);
    (*lookup)->h.import = (import_t*)(&((uint8_t*)*lookup)[size]);
    for (i=0; i<m->count.import; i++) {
      import = (import_t*)(&((uint8_t*)*lookup)[size]);
      lookup_name(buf, LEN, &import->mod);
      lookup_name(buf, LEN, &import->nm);
      type = (*buf)[0];
      (*buf)++;
      import->d.type = type;
      switch (type) {
        case 0x00:
          leb_u32(buf, LEN, &u32);
	  import->d.u.x = u32;
	  break;
	case 0x01:
	  import->d.u.tt.et = (*buf)[0];
	  (*buf)++;
	  lookup_limits(buf, LEN, &import->d.u.tt.lim);
	  break;
	case 0x02:
	  lookup_limits(buf, LEN, &import->d.u.mt.lim);
	  break;
	case 0x03:
	  import->d.u.gt.t = (*buf)[0];
	  (*buf)++;
	  import->d.u.gt.m = (*buf)[0];
	  (*buf)++;
	  break;
	default:
	  assert(false);
      }
      size += sizeof(struct import_st);
    }
#if DEBUG_LOOKUP
    for (i=0; i<m->count.import; i++) {
      import = &(*lookup)->h.import[i];
      printf("\n" NEST "(import \"");
      print_utf(import->mod.len, import->mod.s);
      printf("\" \"");
      print_utf(import->nm.len, import->nm.s);
      printf("\"");
      switch (import->d.type) {
        case 0x00:
          printf(" (func (;%d;) (type %d))", (*lookup)->h.n_func, import->d.u.x);
          (*lookup)->h.n_func++;
	  break;
	case 0x01:
	  printf(" (table?)");
	  break;
	case 0x02:
	  printf(" (mem?)");
	  break;
	case 0x03:
	  printf(" (global?)");
	  break;
	default:
	  assert(false);
      }
      printf(")");
    }
#endif
  } else {
    size += sizeof(struct import_st) * m->count.import;
  }
  return size;
}

struct func_st {
  typeidx_t typeidx;
  struct {
    uint32_t        size;
    uint32_t        locals_len;
    locals_t       *locals;
    struct expr_st  expr;
  } code;
};

size_t lookup_alloc_function_and_code_sections(module_t *m, lookup_cache_t **lookup, size_t size)
{
  size_t          i         = 0;
  size_t          j         = 0;
  uint32_t        u32       = 0;
  size_t          len       = m->section[3].len;
  const uint8_t  *start_buf = m->section[3].buf;
  const uint8_t  *bufptr    = m->section[3].buf;
  const uint8_t **buf       = &bufptr;
  func_t         *func      = NULL;
  uint8_t         type      = 0;

  assert(m->count.func == m->count.code);

  if (lookup) {
    leb_u32(buf, LEN, &u32);
    assert(u32 == m->count.func);
    (*lookup)->h.func = (func_t*)(&((uint8_t*)*lookup)[size]);
    for (i=0; i<m->count.func; i++) {
      func = (func_t*)(&((uint8_t*)*lookup)[size]);
      leb_u32(buf, LEN, &u32);
      func->typeidx = u32;
      size += sizeof(struct func_st);
    }
    len       = m->section[10].len;
    start_buf = m->section[10].buf;
    bufptr    = m->section[10].buf;
    leb_u32(buf, LEN, &u32);
    assert(u32 == m->count.code);
    for (i=0; i<m->count.code; i++) {
      func = &(*lookup)->h.func[i];
      leb_u32(buf, LEN, &u32);
      func->code.size = u32;
      leb_u32(buf, LEN, &u32);
      func->code.locals_len = u32;
      func->code.locals = (locals_t*)(&((uint8_t*)*lookup)[size]);
      for (j=0; j<func->code.locals_len; j++) {
        leb_u32(buf, LEN, &u32);
	func->code.locals[j].n = u32;
	func->code.locals[j].t = (*buf)[0];
	(*buf)++;
	size += sizeof(struct locals_st);
      }
      lookup_expr(buf, LEN, &func->code.expr);
    }
#if DEBUG_LOOKUP
    for (i=0; i<m->count.func; i++) {
      func = &(*lookup)->h.func[i];
      printf("\n" NEST "(func (;%d;) (type %d)", m->count.import + i, func->typeidx);
      print_wat_func_sig(lookup, func->typeidx);
      if (func->code.locals_len > 0) {
        char locals_buf[128] = { 0 };
        dis_locals(func->code.locals, func->code.locals_len, locals_buf, sizeof(locals_buf));
        printf("\n" NEST NEST "%s", locals_buf);
      }
      dis_expr(func->code.expr.in, func->code.size, true);
      (*lookup)->h.n_func++;
    }
#endif
  } else {
    size += sizeof(struct locals_st) * m->count.locals;
    size += sizeof(struct func_st) * m->count.func;
  }
  return size;
}

/*
typedef struct tabletype_st {
  reftype_t et;
  struct limits_st lim;
} tabletype_t;
*/

size_t lookup_alloc_table_section(module_t *m, lookup_cache_t **lookup, size_t size)
{
  size_t          i           = 0;
  uint32_t        u32         = 0;
  uint32_t        table_count = 0;
  tabletype_t    *table       = NULL;
  size_t          len         = m->section[4].len;
  const uint8_t  *start_buf   = m->section[4].buf;
  const uint8_t  *bufptr      = m->section[4].buf;
  const uint8_t **buf         = &bufptr;

  leb_u32(buf, 5, &table_count);
  /* since tabletypes can be used in other places the count may be higher than the number
   * in the table section. */
  if (lookup) {
    (*lookup)->h.table = (tabletype_t*)(&((uint8_t*)*lookup)[size]);
    for (i=0; i<table_count; i++) {
      table = (tabletype_t*)(&((uint8_t*)*lookup)[size]);
      table->et = (*buf)[0];
      (*buf)++;
      lookup_limits(buf, LEN, &(table->lim));
      size += sizeof(struct tabletype_st);
    }
#if DEBUG_LOOKUP
    for (i=0; i<table_count; i++) {
      table = &(*lookup)->h.table[i];
      printf("\n" NEST "(table (;%u;) ", i);
      if (table->lim.type == 0) {
        printf("%u", table->lim.n);
      } else {
        printf("%u %u", table->lim.n, table->lim.m);
      }
      printf(" %s)", dis_reftype(table->et));
    }
#endif
  } else {
    size += sizeof(struct tabletype_st) * table_count;
  }
  return size;
}

/*
struct memtype_st {
  struct limits_st lim;
} memtype_t;
*/
size_t lookup_alloc_memory_section(module_t *m, lookup_cache_t **lookup, size_t size)
{
  size_t          i            = 0;
  uint32_t        u32          = 0;
  uint32_t        memory_count = 0;
  memtype_t      *memory       = NULL;
  size_t          len          = m->section[5].len;
  const uint8_t  *start_buf    = m->section[5].buf;
  const uint8_t  *bufptr       = m->section[5].buf;
  const uint8_t **buf          = &bufptr;

  leb_u32(buf, 5, &memory_count);
  /* since can be used in other places the count may be higher than the number in the section. */
  if (lookup) {
    (*lookup)->h.memory = (memtype_t*)(&((uint8_t*)*lookup)[size]);
    for (i=0; i<memory_count; i++) {
      memory = (memtype_t*)(&((uint8_t*)*lookup)[size]);
      lookup_limits(buf, LEN, &(memory->lim));
      size += sizeof(struct memtype_st);
    }
#if DEBUG_LOOKUP
    for (i=0; i<memory_count; i++) {
      memory = &(*lookup)->h.memory[i];
      printf("\n" NEST "(memory (;%u;) ", i);
      if (memory->lim.type == 0) {
        printf("%u", memory->lim.n);
      } else {
        printf("%u %u", memory->lim.n, memory->lim.m);
      }
      printf(")");
    }
#endif
  } else {
    size += sizeof(struct memtype_st) * memory_count;
  }
  return size;
}

typedef struct global_st {
  struct globaltype_st gt;
  struct expr_st       e;
} global_t;


size_t lookup_alloc_global_section(module_t *m, lookup_cache_t **lookup, size_t size)
{
  size_t          i            = 0;
  uint32_t        u32          = 0;
  uint32_t        global_count = 0;
  global_t       *global       = NULL;
  size_t          len          = m->section[6].len;
  const uint8_t  *start_buf    = m->section[6].buf;
  const uint8_t  *bufptr       = m->section[6].buf;
  const uint8_t **buf          = &bufptr;

  leb_u32(buf, 5, &global_count);
  /* since can be used in other places the count may be higher than the number in the section. */
  if (lookup) {
    (*lookup)->h.global = (global_t*)(&((uint8_t*)*lookup)[size]);
    for (i=0; i<global_count; i++) {
      global = (global_t*)(&((uint8_t*)*lookup)[size]);
      global->gt.t = (*buf)[0];
      (*buf)++;
      global->gt.m = (*buf)[0];
      (*buf)++;
      lookup_expr(buf, LEN, &global->e);
      size += sizeof(struct global_st);
    }
#if DEBUG_LOOKUP
    for (i=0; i<global_count; i++) {
      global = &(*lookup)->h.global[i];
      printf("\n" NEST "(global (;%u;) ", i);
      if (global->gt.m == 0x01) {
        printf("(mut %s)", dis_valtype(global->gt.t));
      } else {
        printf("%s", dis_valtype(global->gt.t));
      }
      printf(" (");
      dis_expr(global->e.in, (global->e.end - global->e.in), false);
      printf(")");
    }
#endif
  } else {
    size += sizeof(struct global_st) * global_count;
  }
  return size;
}

typedef enum {
  EXPORTDESC_TYPE_FUNC   = 0,
  EXPORTDESC_TYPE_TABLE  = 1,
  EXPORTDESC_TYPE_MEM    = 2,
  EXPORTDESC_TYPE_GLOBAL = 3,
} exportdesc_type_t;

typedef struct exportdesc_st {
  exportdesc_type_t type;
  uint32_t          idx;
} exportdesc_t;

struct export_st {
  struct name_st       nm;
  struct exportdesc_st d;
};

size_t lookup_alloc_export_section(module_t *m, lookup_cache_t **lookup, size_t size)
{
  size_t          i            = 0;
  uint32_t        u32          = 0;
  uint32_t        export_count = 0;
  export_t       *export       = NULL;
  size_t          len          = m->section[7].len;
  const uint8_t  *start_buf    = m->section[7].buf;
  const uint8_t  *bufptr       = m->section[7].buf;
  const uint8_t **buf          = &bufptr;

  leb_u32(buf, 5, &export_count);
  /* since can be used in other places the count may be higher than the number in the section. */
  if (lookup) {
    (*lookup)->h.export   = (export_t*)(&((uint8_t*)*lookup)[size]);
    (*lookup)->h.n_export = export_count;
    for (i=0; i<export_count; i++) {
      export = (export_t*)(&((uint8_t*)*lookup)[size]);
      lookup_name(buf, LEN, &export->nm);
      export->d.type = (*buf)[0];
      (*buf)++;
      leb_u32(buf, LEN, &export->d.idx);
      size += sizeof(struct export_st);
    }
#if DEBUG_LOOKUP
    for (i=0; i<export_count; i++) {
      export = &(*lookup)->h.export[i];
      printf("\n" NEST "(export \"");
      print_utf(export->nm.len, export->nm.s);
      printf("\" (%s %u))", dis_exportdesc(export->d.type), export->d.idx);
    }
#endif
  } else {
    size += sizeof(struct export_st) * export_count;
  }
  return size;
}

struct data_st {
  uint8_t         type;
  struct expr_st  e;
  uint32_t        blen;
  const uint8_t  *b;
  uint32_t        x;
};

size_t lookup_alloc_data_section(module_t *m, lookup_cache_t **lookup, size_t size)
{
  size_t          i            = 0;
  uint32_t        u32          = 0;
  uint32_t        data_count   = 0;
  data_t         *data         = NULL;
  size_t          len          = m->section[11].len;
  const uint8_t  *start_buf    = m->section[11].buf;
  const uint8_t  *bufptr       = m->section[11].buf;
  const uint8_t **buf          = &bufptr;

  leb_u32(buf, LEN, &data_count);
  /* since can be used in other places the count may be higher than the number in the section. */
  if (lookup) {
    (*lookup)->h.data = (data_t*)(&((uint8_t*)*lookup)[size]);
    for (i=0; i<data_count; i++) {
      data = (data_t*)(&((uint8_t*)*lookup)[size]);
      data->type = (*buf)[0];
      (*buf)++;
      if (data->type == 2) {
        leb_u32(buf, LEN, &data->x);
      }
      if (data->type == 0 || data->type == 2) {
        lookup_expr(buf, LEN, &data->e);
      }
      leb_u32(buf, LEN, &u32);
      data->blen = u32;
      data->b = (*buf);
      (*buf) += u32;
      size += sizeof(struct data_st);
    }
#if DEBUG_LOOKUP
    for (i=0; i<data_count; i++) {
      data = &(*lookup)->h.data[i];
      printf("\n" NEST "(data (;%u;)", i);
      if (data->type == 2) {
        printf(" %u", data->x);
      }
      if (data->type == 0 || data->type == 2) {
        printf(" (");
        dis_expr(data->e.in, data->e.end - data->e.in, false);
	printf(")");
      }
      printf(" \"");
      print_data(data->b, data->blen);
      printf("\")");
    }
#endif
  } else {
    size += sizeof(struct data_st) * data_count;
  }
  return size;
}

size_t lookup_alloc(module_t *m, lookup_cache_t **lookup)
{
  size_t size = sizeof((*lookup)->h);

  if (lookup != NULL) {
    (*lookup)->h.m = m;
  }
#if DEBUG_LOOKUP
  printf("(module");
#endif

  size = lookup_alloc_type_section(m, lookup, size);
  size = lookup_alloc_import_section(m, lookup, size);
  size = lookup_alloc_function_and_code_sections(m, lookup, size);
  size = lookup_alloc_table_section(m, lookup, size);
  size = lookup_alloc_memory_section(m, lookup, size);
  size = lookup_alloc_global_section(m, lookup, size);
  size = lookup_alloc_export_section(m, lookup, size);
  size = lookup_alloc_data_section(m, lookup, size);

#if DEBUG_LOOKUP
  if (lookup == NULL) {
    print_counts(m);
    DEBUG("size calculation: %zu\n", size);
  } else {
    printf(")\n"); /* Final close paren */
  }
#endif
  return size;
}

uint32_t lookup_export(lookup_cache_t *lookup, const char *name)
{
  export_t *export  = NULL;
  size_t    i       = 0;
  if (lookup == NULL || lookup->h.export == NULL)
    goto fail;
  for (i=0; i<lookup->h.n_export; i++) {
    export = &lookup->h.export[i];
    if (export->d.type == EXPORTDESC_TYPE_FUNC) {
      size_t len = strlen(name);
      if (len == export->nm.len && memcmp(export->nm.s, name, len) == 0)
        return export->d.idx;
    }
  }
fail:
  return UINT32_MAX;
}

