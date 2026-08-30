#include "validate.h"
#include "leb128.h"
#include <stdio.h>
#include <string.h>

#define LEN (len - (*buf - start_buf))

#define DEBUG_VALIDATE 0

#if DEBUG_VALIDATE
#include <stdio.h>
#define DEBUG(...) printf("%s: %d: ", __FILE__, __LINE__); printf(__VA_ARGS__)
#else
#define DEBUG(...) { }
#endif

static struct module_st module_tmp = { 0 };

void print_counts(module_t *m)
{
  printf("numtype:    %d\n", m->count.numtype);
  printf("vectype:    %d\n", m->count.vectype);
  printf("reftype:    %d\n", m->count.reftype);
  printf("valtype:    %d\n", m->count.valtype);
  printf("functype:   %d\n", m->count.functype);
  printf("name:       %d\n", m->count.name);
  printf("mut:        %d\n", m->count.mut);
  printf("globaltype: %d\n", m->count.globaltype);
  printf("memtype:    %d\n", m->count.memtype);
  printf("tabletype:  %d\n", m->count.tabletype);
  printf("typeidx:    %d\n", m->count.typeidx);
  printf("importdesc: %d\n", m->count.importdesc);
  printf("import:     %d\n", m->count.import);
  printf("blocktype:  %d\n", m->count.blocktype);
  printf("tableidx:   %d\n", m->count.tableidx);
  printf("funcidx:    %d\n", m->count.funcidx);
  printf("localidx:   %d\n", m->count.localidx);
  printf("globalidx:  %d\n", m->count.globalidx);
  printf("labelidx:   %d\n", m->count.labelidx);
  printf("elemidx:    %d\n", m->count.elemidx);
  printf("memidx:     %d\n", m->count.memidx);
  printf("dataidx:    %d\n", m->count.dataidx);
  printf("laneidx:    %d\n", m->count.laneidx);
  printf("memarg:     %d\n", m->count.memarg);
  printf("expr:       %d\n", m->count.expr);
  printf("exportdesc: %d\n", m->count.exportdesc);
  printf("export:     %d\n", m->count.export);
  printf("global:     %d\n", m->count.global);
  printf("locals:     %d\n", m->count.locals);
  printf("code:       %d\n", m->count.code);
  printf("data:       %d\n", m->count.data);
  printf("elem:       %d\n", m->count.elem);
  printf("elemkind:   %d\n", m->count.elemkind);
  printf("limits:     %d\n", m->count.limits);
  printf("instr:      %d\n", m->count.instr);
  printf("resulttype: %d\n", m->count.resulttype);
  printf("func:       %d\n", m->count.func);
}



bool is_ending_0x0B(const uint8_t byte)
{
  return (byte == 0x0B);
}

bool is_ending_0x0B_or_0x05(const uint8_t byte)
{
  return (byte == 0x0B) || (byte == 0x05);
}

bool is_valid_numtype(const uint8_t **buf, size_t len, module_t *m)
{
  if (len < 1)
    goto invalid;
  switch(*buf[0]) {
    case 0x7F:
    case 0x7E:
    case 0x7D:
    case 0x7C:
      (*buf)++;
      m->count.numtype++;
      return true;
    default:
      break;
  }
invalid:
  DEBUG("invalid numtype\n");
  return false;
}

bool is_valid_vectype(const uint8_t **buf, size_t len, module_t *m)
{
  if (len < 1)
    goto invalid;
  if (*buf[0] == 0x7B) {
    (*buf)++;
    m->count.vectype++;
    return true;
  }
invalid:
  DEBUG("invalid vectype\n");
  return false;
}

bool is_valid_reftype(const uint8_t **buf, size_t len, module_t *m)
{
  if (len < 1)
    goto invalid;
  switch (*buf[0]) {
    case 0x70:
    case 0x6F:
      (*buf)++;
      m->count.reftype++;
      return true;
    default:
      break;
  }
invalid:
  DEBUG("invalid reftype\n");
  return false;
}

bool is_valid_valtype(const uint8_t **buf, size_t len, module_t *m)
{
  if (!is_valid_numtype(buf, len, m) &&
      !is_valid_vectype(buf, len, m) &&
      !is_valid_reftype(buf, len, m)) {
    DEBUG("invalid valtype\n");
    return false;
  }
  m->count.valtype++;
  return true;
}

bool is_valid_vec(const uint8_t **buf, size_t len, module_t *m, is_validator_t is_validator)
{
  size_t         i;
  uint32_t       n;
  const uint8_t *start_buf = *buf;
  if (!leb_u32(buf, len, &n))
    goto invalid;
  for (i=0; i<n; i++) {
    if (!is_validator(buf, LEN, m))
      goto invalid;
  }
  return true;
invalid:
  DEBUG("invalid vec\n");
  *buf = start_buf;
  return false;
}

bool is_valid_resulttype(const uint8_t **buf, size_t len, module_t *m)
{
  const uint8_t *start_buf = *buf;
  if (!is_valid_vec(buf, LEN, m, is_valid_valtype))
    goto invalid;
valid:
  m->count.resulttype++;
  return true;
invalid:
  DEBUG("invalid resulttype\n");
  *buf = start_buf;
  return false;
}

bool is_valid_functype(const uint8_t **buf, size_t len, module_t *m)
{
  const uint8_t *start_buf = *buf;
  if (len < 1 || (*buf)[0] != 0x60)
    goto invalid;
  (*buf)++;
  if (!is_valid_resulttype(buf, LEN, m) ||
      !is_valid_resulttype(buf, LEN, m))
    goto invalid;

valid:
  m->count.functype++;
  return true;

invalid:
  DEBUG("invalid functype\n");
  *buf = start_buf;
  return false;
}

bool is_valid_byte(const uint8_t **buf, size_t len, module_t *m)
{
  if (len < 1)
    return false;
  (*buf)++;
  /* No need to record these in the counts, we can use the memory segment as is */
  return true;
}

bool is_valid_name(const uint8_t **buf, size_t len, module_t *m)
{
  const uint8_t *start_buf = *buf;
  if (!is_valid_vec(buf, LEN, m, is_valid_byte)) {
    *buf = start_buf;
    return false;
  }
  m->count.name++;
  return true;
}

bool is_valid_mut(const uint8_t **buf, size_t len, module_t *m)
{
  if (len < 1) {
    return false;
  }
  switch (*buf[0]) {
    case 0x00:
    case 0x01:
      (*buf)++;
      m->count.mut++;
      return true;
    default:
      break;
  }
  return false;
}

bool is_valid_globaltype(const uint8_t **buf, size_t len, module_t *m)
{
  const uint8_t *start_buf = *buf;
  if (!is_valid_valtype(buf, LEN, m) ||
      !is_valid_mut    (buf, LEN, m)) {
    *buf = start_buf;
    return false;
  }
  m->count.globaltype++;
  return true;
}

bool is_valid_limits(const uint8_t **buf, size_t len, module_t *m)
{
  uint32_t       min;
  uint32_t       max;
  const uint8_t *start_buf = *buf;
  uint8_t        byte;

  if (len < 1) {
    return false;
  }
  byte = *buf[0];
  (*buf)++;
  switch(byte) {
    case 0x01:
      if (!leb_u32(buf, LEN, &min) ||
          !leb_u32(buf, LEN, &max))
        break;
      m->count.limits++;
      return true;
    case 0x00:
      if (!leb_u32(buf, LEN, &min))
        break;
      m->count.limits++;
      return true;
    default:
      break;
  }
  *buf = start_buf;
  return false;
}

bool is_valid_memtype(const uint8_t **buf, size_t len, module_t *m)
{
  if (!is_valid_limits(buf, len, m))
    return false;
  m->count.memtype++;
  return true;
}

bool is_valid_tabletype(const uint8_t **buf, size_t len, module_t *m)
{
  const uint8_t *start_buf = *buf;
  if (!is_valid_reftype(buf, LEN, m) ||
      !is_valid_limits (buf, LEN, m)) {
    *buf = start_buf;
    return false;
  }
  m->count.tabletype++;
  return true;
}

bool is_valid_typeidx(const uint8_t **buf, size_t len, module_t *m)
{
  uint32_t typeidx;
  if (!leb_u32(buf, len, &typeidx))
    return false;
  m->count.typeidx++;
  return true;
}

bool is_valid_func(const uint8_t **buf, size_t len, module_t *m)
{
  bool res = is_valid_typeidx(buf, len, m);
  if (res) {
    m->count.func++;
  }
  return res;
}

bool is_valid_importdesc(const uint8_t **buf, size_t len, module_t *m)
{
  const uint8_t *start_buf = *buf;
  uint8_t        byte;
  if (len < 1)
    return false;
  byte = (*buf)[0];
  (*buf)++;
  switch(byte) {
    case 0x00:
      if (!is_valid_typeidx(buf, LEN, m))
        break;
      m->count.importdesc++;
      return true;
    case 0x01:
      if (!is_valid_tabletype(buf, LEN, m))
        break;
      m->count.importdesc++;
      return true;
    case 0x02:
      if (!is_valid_memtype(buf, LEN, m))
        break;
      m->count.importdesc++;
      return true;
    case 0x03:
      if (!is_valid_globaltype(buf, LEN, m))
        break;
      m->count.importdesc++;
      return true;
    default:
      break;
  }
  *buf = start_buf;
  return false;
}

bool is_valid_import(const uint8_t **buf, size_t len, module_t *m)
{
  const uint8_t *start_buf = *buf;
  if (!is_valid_name(buf, LEN, m) ||
      !is_valid_name(buf, LEN, m) ||
      !is_valid_importdesc(buf, LEN, m)) {
    *buf = start_buf;
    return false;
  }
  m->count.import++;
  return true;
}

bool is_valid_blocktype(const uint8_t **buf, size_t len, module_t *m)
{
  const uint8_t *start_buf = *buf;
  uint8_t        s33[5]    = { 0 };

  DEBUG("is_valid_blocktype(%p,%zu,%p)\n", buf, len, m);

  if (len < 1)
    return false;

  if ((*buf)[0] == 0x40) {
    (*buf)++;
    DEBUG("blocktype is epsilon\n");
    goto valid;
  }

  if (is_valid_valtype(buf, LEN, m)) {
    DEBUG("blocktype is valtype\n");
    goto valid; 
  }

  if (leb_s(33, buf, LEN, s33, 5)) {
    DEBUG("blocktype is x: %02x%02x%02x%02x%02x\n", s33[0], s33[1], s33[2], s33[3], s33[4]);
    goto valid;
  }

invalid:
  *buf = start_buf;
  return false;

valid:
  m->count.blocktype++;
  return true;
}

bool is_valid_tableidx(const uint8_t **buf, size_t len, module_t *m)
{
  uint32_t x;
  if (!leb_u32(buf, len, &x))
    return false;
  m->count.tableidx++;
  return true;
}

bool is_valid_funcidx(const uint8_t **buf, size_t len, module_t *m)
{
  uint32_t x;
  if (!leb_u32(buf, len, &x))
    return false;
  m->count.funcidx++;
  return true;
}

bool is_valid_localidx(const uint8_t **buf, size_t len, module_t *m)
{
  uint32_t x;
  if (!leb_u32(buf, len, &x))
    return false;
  DEBUG("localidx: %u\n", x);
  m->count.localidx++;
  return true;
}

bool is_valid_globalidx(const uint8_t **buf, size_t len, module_t *m)
{
  uint32_t x;
  if (!leb_u32(buf, len, &x))
    return false;
  m->count.globalidx++;
  return true;
}

bool is_valid_labelidx(const uint8_t **buf, size_t len, module_t *m)
{
  uint32_t l;
  if (!leb_u32(buf, len, &l))
    return false;
  m->count.labelidx++;
  return true;
}

bool is_valid_elemidx(const uint8_t **buf, size_t len, module_t *m)
{
  uint32_t x;
  if (!leb_u32(buf, len, &x))
    return false;
  m->count.elemidx++;
  return true;
}

bool is_valid_memidx(const uint8_t **buf, size_t len, module_t *m)
{
  uint32_t x;
  if (!leb_u32(buf, len, &x))
    return false;
  m->count.memidx++;
  return true;
}

bool is_valid_dataidx(const uint8_t **buf, size_t len, module_t *m)
{
  uint32_t x;
  if (!leb_u32(buf, len, &x))
    return false;
  m->count.dataidx++;
  return true;
}

bool is_valid_laneidx(const uint8_t **buf, size_t len, module_t *m)
{
  if (len < 1)
    return false;
  (*buf)++;
  m->count.laneidx++;
  return true;
}

bool is_valid_memarg(const uint8_t **buf, size_t len, module_t *m)
{
  uint32_t       a;
  uint32_t       o;
  const uint8_t *start_buf = *buf;
  if (!leb_u32(buf, LEN, &a) ||
      !leb_u32(buf, LEN, &o)) {
    *buf = start_buf;
    return false;
  }
  m->count.memarg++;
  return true;
}

bool is_valid_instrs(const uint8_t **buf, size_t len, module_t *m, is_ending_t is_ending)
{
  const uint8_t *start_buf = *buf;
  while (!is_ending(*buf[0]) && LEN > 0) {
    if (!is_valid_instr(buf, LEN, m)) {
      *buf = start_buf;
      return false;
    }
  }
  if (LEN < 1) {
    *buf = start_buf;
    return false;
  }
  (*buf)++;
  return true;
}

bool is_valid_instr(const uint8_t **buf, size_t len, module_t *m)
{
  const uint8_t   *start_buf = *buf;
  uint8_t          byte      = 0;
  uint8_t          op        = 0;
  uint32_t         subop     = 0;
  int32_t          i32       = 0;
  int64_t          i64       = 0;
  float            f32       = 0;
  double           f64       = 0;

  if (m == NULL) {
    m = &module_tmp;
  }

  if (len < 1)
    goto invalid; 
  op = (*buf)[0];
  (*buf)++;
  DEBUG("evaluate op 0x%02X, len %zu\n", op, len);
  switch (op) {
/* Control Instructions */
/*----------------------------------------------------------------------------*/
    case 0x00: DEBUG("[unreachable]\n"); goto valid;
    case 0x01: DEBUG("[nop]\n"); goto valid;
    case 0x0F: DEBUG("[return]\n"); goto valid;
    case 0x02: // block bt in* end
      DEBUG("[block bt in* end]\n");
      if (!is_valid_blocktype(buf, LEN, m) ||
          !is_valid_instrs(buf, LEN, m, is_ending_0x0B))
        goto invalid;
      goto valid;
    case 0x03: // loop bt in* end
      DEBUG("[loop bt in* end]\n");
      if (!is_valid_blocktype(buf, LEN, m) ||
          !is_valid_instrs(buf, LEN, m, is_ending_0x0B))
        goto invalid;
      goto valid;
    case 0x04: // if bt in* { else in* } end
      DEBUG("[if bt in* { else in* } end]\n");
      if (!is_valid_blocktype(buf, LEN, m) ||
          !is_valid_instrs(buf, LEN, m, is_ending_0x0B_or_0x05))
        goto invalid;
      (*buf)--;
      byte = (*buf)[0];
      (*buf)++;
      if (byte == 0x0B)
        goto valid;
      if (!is_valid_instrs(buf, LEN, m, is_ending_0x0B))
        goto invalid;
      goto valid;
    case 0x0C: // br l
      DEBUG("[br l]\n");
      if (!is_valid_labelidx(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x0D: // br_if l
      DEBUG("[br_if l]\n");
      if (!is_valid_labelidx(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x0E: // br_table l* lN
      DEBUG("[br_table l*]\n");
      if (!is_valid_vec(buf, LEN, m, is_valid_labelidx) ||
          !is_valid_labelidx(buf, LEN, m))
	goto invalid;
      goto valid;
    case 0x10: // call x
      DEBUG("[call x]\n");
      if (!is_valid_funcidx(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x11: // call_indirect x y
      DEBUG("[call_indirect x y]\n");
      if (!is_valid_typeidx(buf, LEN, m) ||
          !is_valid_tableidx(buf, LEN, m))
        goto invalid;
      goto valid;
/* Parametric Instructions */
/*----------------------------------------------------------------------------*/
    case 0x1A: // drop
      DEBUG("[drop]\n");
      goto valid;
    case 0x1B: // select
      DEBUG("[select]\n");
      goto valid;
    case 0x1C: // select t*
      DEBUG("[select t*]\n");
      if (!is_valid_vec(buf, LEN, m, is_valid_valtype))
        goto invalid;
      goto valid;
/* Variable Instructions */
/*----------------------------------------------------------------------------*/
    case 0x20: // local.get x
      DEBUG("[local.get x]\n");
      if (!is_valid_localidx(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x21: // local.set x
      DEBUG("[local.set x]\n");
      if (!is_valid_localidx(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x22: // local.tee x
      DEBUG("[local.tee x]\n");
      if (!is_valid_localidx(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x23: // global.get x
      DEBUG("[global.get x]\n");
      if (!is_valid_globalidx(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x24: // global.set x
      DEBUG("[global.set x]\n");
      if (!is_valid_globalidx(buf, LEN, m))
        goto invalid;
      goto valid;
/* Table Instructions */
/*----------------------------------------------------------------------------*/
    case 0x25:
      DEBUG("[table.get x]\n");
      if (!is_valid_tableidx(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x26:
      DEBUG("[table.set x]\n");
      if (!is_valid_tableidx(buf, LEN, m))
        goto invalid;
      goto valid;
    // 0xFC instructions handled in shared case below
/* Memory Instructions */
/*----------------------------------------------------------------------------*/
    case 0x28: // i32.load m
      DEBUG("[i32.load m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x29: // i64.load m
      DEBUG("[i64.load m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x2A: // f32.load m
      DEBUG("[f32.load m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x2B: // f64.load m
      DEBUG("[f64.load m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x2C: // i32.load8_s m
      DEBUG("[i32.load8_s m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x2D: // i32.load8_u m
      DEBUG("[i32.load8_u m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x2E: // i32.load16_s m
      DEBUG("[i32.load16_s m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x2F: // i32.load16_u m
      DEBUG("[i32.load16_u m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x30: // i64.load8_s m
      DEBUG("[i64.load8_s m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x31: // i64.load8_u m
      DEBUG("[i64.load8_u m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x32: // i64.load16_s m
      DEBUG("[i64.load16_s m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x33: // i64.load16_u m
      DEBUG("[i64.load16_u m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x34: // i64.load32_s m
      DEBUG("[i64.load32_s m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x35: // i64.load32_u m
      DEBUG("[i64.load32_u m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x36: // i32.store m
      DEBUG("[i32.store m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x37: // i64.store m
      DEBUG("[i64.store m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x38: // f32.store m
      DEBUG("[f32.store m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x39: // f64.store m
      DEBUG("[f64.store m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x3A: // i32.store8 m
      DEBUG("[i32.store8 m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x3B: // i32.store16 m
      DEBUG("[i32.store16 m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x3C: // i64.store8 m
      DEBUG("[i64.store8 m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x3D: // i64.store16 m
      DEBUG("[i64.store16 m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x3E: // i64.store32 m
      DEBUG("[i64.store32 m]\n");
      if (!is_valid_memarg(buf, LEN, m))
        goto invalid;
      goto valid;
    case 0x3F:
      DEBUG("[memory.size]\n");
      if ((*buf)[0] != 0x00)
        goto invalid;
      (*buf)++;
      goto valid;
    case 0x40:
      DEBUG("[memory.grow]\n");
      if ((*buf)[0] != 0x00)
        goto invalid;
      (*buf)++;
      goto valid;
/* Numeric Instructions */
/*----------------------------------------------------------------------------*/
    case 0x41: // i32.const n
      DEBUG("[i32.const n]\n");
      if (!leb_i32(buf, LEN, &i32))
        goto invalid;
      goto valid;
    case 0x42: // i64.const n
      DEBUG("[i64.const n]\n");
      if (!leb_i64(buf, LEN, &i64))
        goto invalid;
      goto valid;
    case 0x43: // f32.const z
      DEBUG("[f32.const z]\n");
      if (LEN < 4)
        goto invalid;
      (*buf) += 4;
      goto valid;
    case 0x44: // f64.const z
      DEBUG("[f64.const z]\n");
      if (LEN < 8)
        goto invalid;
      (*buf) += 8;
      goto valid;
    case 0x45: // i32.eqz
    case 0x46: // i32.eq
    case 0x47: // i32.ne
    case 0x48: // i32.lt_s
    case 0x49: // i32.lt_u
    case 0x4A: // i32.gt_s
    case 0x4B: // i32.gt_u
    case 0x4C: // i32.le_s
    case 0x4D: // i32.le_u
    case 0x4E: // i32.ge_s
    case 0x4F: // i32.ge_u

    case 0x50: // i64.eqz
    case 0x51: // i64.eq
    case 0x52: // i64.ne
    case 0x53: // i64.lt_s
    case 0x54: // i64.lt_u
    case 0x55: // i64.gt_s
    case 0x56: // i64.gt_u
    case 0x57: // i64.le_s
    case 0x58: // i64.le_u
    case 0x59: // i64.ge_s
    case 0x5A: // i64.ge_u

    case 0x5B: // f32.eq
    case 0x5C: // f32.ne
    case 0x5D: // f32.lt
    case 0x5E: // f32.gt
    case 0x5F: // f32.le
    case 0x60: // f32.ge

    case 0x61: // f64.eq
    case 0x62: // f64.ne
    case 0x63: // f64.lt
    case 0x64: // f64.gt
    case 0x65: // f64.le
    case 0x66: // f64.ge

    case 0x67: // i32.clz
    case 0x68: // i32.ctz
    case 0x69: // i32.popcnt
    case 0x6A: // i32.add
    case 0x6B: // i32.sub
    case 0x6C: // i32.mul
    case 0x6D: // i32.div_s
    case 0x6E: // i32.div_u
    case 0x6F: // i32.rem_s
    case 0x70: // i32.rem_u
    case 0x71: // i32.and
    case 0x72: // i32.or
    case 0x73: // i32.xor
    case 0x74: // i32.shl
    case 0x75: // i32.shr_s
    case 0x76: // i32.shr_u
    case 0x77: // i32.rotl
    case 0x78: // i32.rotr

    case 0x79: // i64.clz
    case 0x7A: // i64.ctz
    case 0x7B: // i64.popcnt
    case 0x7C: // i64.add
    case 0x7D: // i64.sub
    case 0x7E: // i64.mul
    case 0x7F: // i64.div_s
    case 0x80: // i64.div_u
    case 0x81: // i64.rem_s
    case 0x82: // i64.rem_u
    case 0x83: // i64.and
    case 0x84: // i64.or
    case 0x85: // i64.xor
    case 0x86: // i64.shl
    case 0x87: // i64.shr_s
    case 0x88: // i64.shr_u
    case 0x89: // i64.rotl
    case 0x8A: // i64.rotr

    case 0x8B: // f32.abs
    case 0x8C: // f32.neg
    case 0x8D: // f32.ceil
    case 0x8E: // f32.floor
    case 0x8F: // f32.trunc
    case 0x90: // f32.nearest
    case 0x91: // f32.sqrt
    case 0x92: // f32.add
    case 0x93: // f32.sub
    case 0x94: // f32.mul
    case 0x95: // f32.div
    case 0x96: // f32.min
    case 0x97: // f32.max
    case 0x98: // f32.copysign

    case 0x99: // f64.abs
    case 0x9A: // f64.neg
    case 0x9B: // f64.ceil
    case 0x9C: // f64.floor
    case 0x9D: // f64.trunc
    case 0x9E: // f64.nearest
    case 0x9F: // f64.sqrt
    case 0xA0: // f64.add
    case 0xA1: // f64.sub
    case 0xA2: // f64.mul
    case 0xA3: // f64.div
    case 0xA4: // f64.min
    case 0xA5: // f64.max
    case 0xA6: // f64.copysign

    case 0xA7: // i32.wrap_i64
    case 0xA8: // i32.trunc_f32_s
    case 0xA9: // i32.trunc_f32_u
    case 0xAA: // i32.trunc_f64_s
    case 0xAB: // i32.trunc_f64_u
    case 0xAC: // i64.extend_i32_s
    case 0xAD: // i64.extend_i32_u
    case 0xAE: // i64.trunc_f32_s
    case 0xAF: // i64.trunc_f32_u
    case 0xB0: // i64.trunc_f64_s
    case 0xB1: // i64.trunc_f64_u
    case 0xB2: // f32.convert_i32_s
    case 0xB3: // f32.convert_i32_u
    case 0xB4: // f32.convert_i64_s
    case 0xB5: // f32.convert_i64_u
    case 0xB6: // f32.demote_f64
    case 0xB7: // f64.convert_i32_s
    case 0xB8: // f64.convert_i32_u
    case 0xB9: // f64.convert_i64_s
    case 0xBA: // f64.convert_i64_u
    case 0xBB: // f64.promote_f32
    case 0xBC: // i32.reinterpret_f32
    case 0xBD: // i64.reinterpret_f64
    case 0xBE: // f32.reinterpret_i32
    case 0xBF: // f64.reinterpret_i64

    case 0xC0: // i32.extend8_s
    case 0xC1: // i32.extend16_s
    case 0xC2: // i64.extend8_s
    case 0xC3: // i64.extend16_s
    case 0xC4: // i64.extend32_s
      goto valid;
/* Vector Instructions */
/*----------------------------------------------------------------------------*/
    case 0xFD:
      if (!leb_u32(buf, LEN, &subop))
        goto invalid;
      switch (subop) {
        case  0: // v128.load m
        case  1: // v128.load8x8_s m
        case  2: // v128.load8x8_u m
        case  3: // v128.load16x4_s m
        case  4: // v128.load16x4_u m
        case  5: // v128.load32x2_s m
        case  6: // v128.load32x2_u m
        case  7: // v128.load8_splat m
        case  8: // v128.load16_splat m
        case  9: // v128.load32_splat m
        case 10: // v128.load64_splat m
        case 92: // v128.load32_zero m
        case 93: // v128.load64_zero m
        case 11: // v128.store m
          if (!is_valid_memarg(buf, LEN, m))
            goto invalid;
          goto valid;
	case 84: // v128.load8_lane m l
	case 85: // v128.load16_lane m l
	case 86: // v128.load32_lane m l
	case 87: // v128.load64_lane m l
	case 88: // v128.store8_lane m l
	case 89: // v128.store16_lane m l
        case 90: // v128.store32_lane m l
        case 91: // v128.store64_lane m l
          if (!is_valid_memarg(buf, LEN, m) ||
              !is_valid_laneidx(buf, LEN, m))
            break;
          goto valid;
	case 12: // v128.const bytes^{-1}_{i128}(b0 ... b15)
          if (LEN < 16)
	    break;
	  (*buf) += 16;
	  goto valid;
	case 13: // i8x16.shuffle l^{16}
	  if (!is_valid_laneidx(buf, LEN, m) ||
	      !is_valid_laneidx(buf, LEN, m) ||
	      !is_valid_laneidx(buf, LEN, m) ||
	      !is_valid_laneidx(buf, LEN, m) ||
	      !is_valid_laneidx(buf, LEN, m) ||
	      !is_valid_laneidx(buf, LEN, m) ||
	      !is_valid_laneidx(buf, LEN, m) ||
	      !is_valid_laneidx(buf, LEN, m) ||
	      !is_valid_laneidx(buf, LEN, m) ||
	      !is_valid_laneidx(buf, LEN, m) ||
	      !is_valid_laneidx(buf, LEN, m) ||
	      !is_valid_laneidx(buf, LEN, m) ||
	      !is_valid_laneidx(buf, LEN, m) ||
	      !is_valid_laneidx(buf, LEN, m) ||
	      !is_valid_laneidx(buf, LEN, m) ||
	      !is_valid_laneidx(buf, LEN, m))
            goto invalid;
	  goto valid;
	case 21: // i8x16.extract_lane_s l
	case 22: // i8x16.extract_lane_u l
	case 23: // i8x16.replace_lane l
	case 24: // i16x8.extract_lane_s l
	case 25: // i16x8.extract_lane_u l
	case 26: // i16x8.replace_lane l
	case 27: // i32x4.extract_lane l
	case 28: // i32x4.replace_lane l
	case 29: // i64x2.extract_lane l
	case 30: // i64x2.replace_lane l
	case 31: // f32x4.extract_lane l
	case 32: // f32x4.replace_lane l
	case 33: // f64x2.extract_lane l
	case 34: // f64x2.replace_lane l
          if (!is_valid_laneidx(buf, LEN, m))
            goto invalid;
          goto valid;
	case 14: // i8x16.swizzle
	case 15: // i8x16.splat
	case 16: // i16x8.splat
	case 17: // i32x4.splat
	case 18: // i64x2.splat
	case 19: // f32x4.splat
	case 20: // f64x2.splat

	case 35: // i8x16.eq
	case 36: // i8x16.ne
	case 37: // i8x16.lt_s
	case 38: // i8x16.lt_u
	case 39: // i8x16.gt_s
	case 40: // i8x16.gt_u
	case 41: // i8x16.le_s
	case 42: // i8x16.le_u
	case 43: // i8x16.ge_s
	case 44: // i8x16.ge_u

	case 45: // i16x8.eq
	case 46: // i16x8.ne
	case 47: // i16x8.lt_s
	case 48: // i16x8.lt_u
	case 49: // i16x8.gt_s
	case 50: // i16x8.gt_u
	case 51: // i16x8.le_s
	case 52: // i16x8.le_u
	case 53: // i16x8.ge_s
	case 54: // i16x8.ge_u

	case 55: // i32x4.eq
	case 56: // i32x4.ne
	case 57: // i32x4.lt_s
	case 58: // i32x4.lt_u
	case 59: // i32x4.gt_s
	case 60: // i32x4.gt_u
	case 61: // i32x4.le_s
	case 62: // i32x4.le_u
	case 63: // i32x4.ge_s
	case 64: // i32x4.ge_u

	case 214: // i64x2.eq
	case 215: // i64x2.ne
	case 216: // i64x2.lt_s
	case 217: // i64x2.gt_s
	case 218: // i64x2.le_s
	case 219: // i64x2.ge_s

	case 65: // f32x4.eq
	case 66: // f32x4.ne
	case 67: // f32x4.lt
	case 68: // f32x4.gt
	case 69: // f32x4.le
	case 70: // f32x4.ge

	case 71: // f64x2.eq
	case 72: // f64x2.ne
	case 73: // f64x2.lt
	case 74: // f64x2.gt
	case 75: // f64x2.le
	case 76: // f64x2.ge

	case 77: // v128.not
	case 78: // v128.and
	case 79: // v128.andnot
	case 80: // v128.or
	case 81: // v128.xor
	case 82: // v128.bitselect
	case 83: // v128.any_true

	case  96: // i8x16.abs
	case  97: // i8x16.neg
	case  98: // i8x16.popcnt
	case  99: // i8x16.all_true
	case 100: // i8x16.bitmask
	case 101: // i8x16.narrow_i16x8_s
	case 102: // i8x16.narrow_i16x8_u
	case 107: // i8x16.shl
	case 108: // i8x16.shr_s
	case 109: // i8x16.shl_u
	case 110: // i8x16.add
	case 111: // i8x16.add_sat_s
	case 112: // i8x16.add_sat_u
	case 113: // i8x16.sub
	case 114: // i8x16.sub_sat_s
	case 115: // i8x16.sub_sat_u
	case 118: // i8x16.min_s
	case 119: // i8x16.min_u
	case 120: // i8x16.max_s
	case 121: // i8x16.max_u
	case 123: // i8x16.avgr_u

        case 124: // i16x8.extadd_pairwise_i8x16_s
        case 125: // i16x8.extadd_pairwise_i8x16_u
        case 128: // i16x8.abs
        case 129: // i16x8.neg
        case 130: // i16x8.q15mulr_sat_s
        case 131: // i16x8.all_true
        case 132: // i16x8.bitmask
        case 133: // i16x8.narrow_i32x4_s
        case 134: // i16x8.narrow_i32x4_u
        case 135: // i16x8.extend_low_i8x16_s
        case 136: // i16x8.extend_high_i8x16_s
        case 137: // i16x8.extend_low_i8x16_u
        case 138: // i16x8.extend_high_i8x16_u
        case 139: // i16x8.shl
        case 140: // i16x8.shr_s
        case 141: // i16x8.shr_u
        case 142: // i16x8.add
        case 143: // i16x8.add_sat_s
        case 144: // i16x8.add_sat_u
        case 145: // i16x8.sub
        case 146: // i16x8.sub_sat_s
        case 147: // i16x8.sub_sat_u
        case 149: // i16x8.mul
        case 150: // i16x8.min_s
        case 151: // i16x8.min_u
        case 152: // i16x8.max_s
        case 153: // i16x8.max_u
        case 155: // i16x8.avgr_u
        case 156: // i16x8.extmul_low_i8x16_s
        case 157: // i16x8.extmul_high_i8x16_s
        case 158: // i16x8.extmul_low_i8x16_u
        case 159: // i16x8.extmul_high_i8x16_u

	case 126: // i32x4.extadd_pairwise_i16x8_s
	case 127: // i32x4.extadd_pairwise_i16x8_u
	case 160: // i32x4.abs
	case 161: // i32x4.neg
	case 163: // i32x4.all_true
	case 164: // i32x4.bitmask
	case 167: // i32x4.extend_low_i16x8_s
	case 168: // i32x4.extend_high_i16x8_s
	case 169: // i32x4.extend_low_i16x8_u
	case 170: // i32x4.extend_high_i16x8_u
	case 171: // i32x4.shl
	case 172: // i32x4.shr_s
	case 173: // i32x4.shr_u
	case 174: // i32x4.add
	case 177: // i32x4.sub
	case 181: // i32x4.mul
	case 182: // i32x4.min_s
	case 183: // i32x4.min_u
	case 184: // i32x4.max_s
	case 185: // i32x4.max_u
	case 186: // i32x4.dot_i16x8_s
	case 188: // i32x4.extmul_low_i16x8_s
	case 189: // i32x4.extmul_high_i16x8_s
	case 190: // i32x4.extmul_low_i16x8_u
	case 191: // i32x4.extmul_high_i16x8_u

	case 192: // i64x2.abs
	case 193: // i64x2.neg
	case 195: // i64x2.all_true
	case 196: // i64x2.bitmask
	case 199: // i64x2.extend_low_i32x4_s
	case 200: // i64x2.extend_high_i32x4_s
	case 201: // i64x2.extend_low_i32x4_u
	case 202: // i64x2.extend_high_i32x4_u
	case 203: // i64x2.shl
	case 204: // i64x2.shr_s
	case 205: // i64x2.shr_u
	case 206: // i64x2.add
	case 209: // i64x2.sub
	case 213: // i64x2.mul
	case 220: // i64x2.extmul_low_i32x4_s
	case 221: // i64x2.extmul_high_i32x4_s
	case 222: // i64x2.extmul_low_i32x4_u
	case 223: // i64x2.extmul_high_i32x4_u

	case 103: // f32x4.ceil
	case 104: // f32x4.floor
	case 105: // f32x4.trunc
	case 106: // f32x4.nearest
	case 224: // f32x4.abs
	case 225: // f32x4.neg
	case 227: // f32x4.sqrt
	case 228: // f32x4.add
	case 229: // f32x4.sub
	case 230: // f32x4.mul
	case 231: // f32x4.div
	case 232: // f32x4.min
	case 233: // f32x4.max
	case 234: // f32x4.pmin
	case 235: // f32x4.pmax

	case 116: // f64x2.ceil
	case 117: // f64x2.floor
	case 122: // f64x2.trunc
	case 148: // f64x2.nearest
	case 236: // f64x2.abs
	case 237: // f64x2.neg
	case 239: // f64x2.sqrt
	case 240: // f64x2.add
	case 241: // f64x2.sub
	case 242: // f64x2.mul
	case 243: // f64x2.div
	case 244: // f64x2.min
	case 245: // f64x2.max
	case 246: // f64x2.pmin
	case 247: // f64x2.pmax

        case 248: // i32x4.trunc_sat_f32x4_s
        case 249: // i32x4.trunc_sat_f32x4_u
        case 250: // f32x4.convert_i32x4_s
        case 251: // f32x4.convert_i32x4_u
        case 252: // i32x4.trunc_sat_f64x2_s_zero
        case 253: // i32x4.trunc_sat_f64x2_u_zero
        case 254: // f64x2.convert_low_i32x4_s
        case 255: // f64x2.convert_low_i32x4_u
        case  94: // f32x4.demote_f64x2_zero
        case  95: // f64x2.promote_low_f32x4
          goto valid;
	default:
          break;
      }
      break;
/* 0xFC merges Table, Memory, and Numeric Instructions */
/*----------------------------------------------------------------------------*/
    case 0xFC:
      if (!leb_u32(buf, LEN, &subop))
        goto invalid;
      switch (subop) {
        /* From Numeric Instructions */
        case 0: // i32.trunc_sat_f32_s
        case 1: // i32.trunc_sat_f32_u
        case 2: // i32.trunc_sat_f32_s
        case 3: // i32.trunc_sat_f32_u
        case 4: // i64.trunc_sat_f32_s
        case 5: // i64.trunc_sat_f32_u
        case 6: // i64.trunc_sat_f32_s
        case 7: // i64.trunc_sat_f32_u
          goto valid;
        /* From Memory Instructions */
        case 8: // memory.init x
          if (!is_valid_dataidx(buf, LEN, m) ||
               (*buf)[0] != 0x00)
	    goto invalid;
	  (*buf)++;
	  goto valid;
	case 9: // data.drop x
          if (!is_valid_dataidx(buf, LEN, m))
	    goto invalid;
	  goto valid;
	case 10: //memory.copy
	  if (LEN < 2 || (*buf)[0] != 0x00 || (*buf)[1] != 0x00)
	    goto invalid;
	  (*buf) += 2;
	  goto valid;
	case 11: //memory.fill
          if (LEN < 1 || (*buf)[0] != 0x00)
	    goto invalid;
	  (*buf)++;
	  goto valid;
	/* From Table Instructions */
        case 12: // table.init x y
        case 14: // table.copy x y
          if (!is_valid_elemidx(buf, LEN, m) ||
              !is_valid_tableidx(buf, LEN, m))
            goto invalid;
	  goto valid;
	case 13: // elem.drop x
          if (!is_valid_elemidx(buf, LEN, m))
            break;
	  goto valid;
        case 15: // table.grow x
        case 16: // table.size x
        case 17: // table.fill x
          if (!is_valid_tableidx(buf, LEN, m))
            goto invalid;
	  goto valid;
	default:
	  break;
      }
      break;

    default:
      break;
  }

invalid:
  DEBUG("invalid instr. { op = 0x%02X, LEN = %zu }\n", op, LEN);
  *buf = start_buf;
  return false;

valid:
  m->count.instr++;
  return true;
}

bool is_valid_expr(const uint8_t **buf, size_t len, module_t *m)
{
  const uint8_t *start_buf = *buf;
  if (!is_valid_instrs(buf, len, m, is_ending_0x0B)) {
    *buf = start_buf;
    return false;
  }
  m->count.expr++;
  return true;
}

bool is_valid_exportdesc(const uint8_t **buf, size_t len, module_t *m)
{
  const uint8_t *start_buf = *buf;
  uint8_t        op;
  if (len < 1)
    return false;
  op = (*buf)[0];
  (*buf)++;
  switch (op) {
    case 0x00: 
      if (!is_valid_funcidx(buf, LEN, m))
        break;
      m->count.exportdesc++;
      return true;
    case 0x01: 
      if (!is_valid_tableidx(buf, LEN, m))
        break;
      m->count.exportdesc++;
      return true;
    case 0x02: 
      if (!is_valid_memidx(buf, LEN, m))
        break;
      m->count.exportdesc++;
      return true;
    case 0x03: 
      if (!is_valid_globalidx(buf, LEN, m))
        break;
      m->count.exportdesc++;
      return true;
    default:
      break;
  }
  *buf = start_buf;
  return false;
}

bool is_valid_export(const uint8_t **buf, size_t len, module_t *m)
{
  const uint8_t *start_buf = *buf;
  if (!is_valid_name(buf, LEN, m) ||
      !is_valid_exportdesc(buf, LEN, m)) {
    *buf = start_buf;
    return false;
  }
  m->count.export++;
  return true;
}

bool is_valid_global(const uint8_t **buf, size_t len, module_t *m)
{
  const uint8_t *start_buf = *buf;

  if (!is_valid_globaltype(buf, LEN, m) ||
      !is_valid_expr(buf, LEN, m)) {
    *buf = start_buf;
    return false;
  }
  m->count.global++;
  return true;
}

bool is_valid_locals(const uint8_t **buf, size_t len, module_t *m)
{
  const uint8_t *start_buf = *buf;
  uint32_t n;
  if (!leb_u32(buf, LEN, &n) ||
      !is_valid_valtype(buf, LEN, m)) {
    *buf = start_buf;
    return false;
  }
  m->count.locals++;
  return true;
}

bool is_valid_code(const uint8_t **buf, size_t len, module_t *m)
{
  const uint8_t *start_buf = *buf;
  uint32_t size;
  if (!leb_u32(buf, LEN, &size)                ||
      !is_valid_vec(buf, LEN, m, is_valid_locals) ||
      !is_valid_expr(buf, LEN, m)) {
    *buf = start_buf;
    return false;
  }
  m->count.code++;
  return true;
}

bool is_valid_data(const uint8_t **buf, size_t len, module_t *m)
{
  const uint8_t *start_buf = *buf;
  uint32_t op;
  if (!leb_u32(buf, LEN, &op)) {
    *buf = start_buf;
    return false;
  }
  switch (op) {
    case 0:
      if (!is_valid_expr(buf, LEN, m) ||
          !is_valid_vec(buf, LEN, m, is_valid_byte))
        break;
      m->count.data++;
      return true;
    case 1:
      if (!is_valid_vec(buf, LEN, m, is_valid_byte))
        break;
      m->count.data++;
      return true;
    case 2:
      if (!is_valid_memidx(buf, LEN, m) ||
          !is_valid_expr(buf, LEN, m)   ||
          !is_valid_vec(buf, LEN, m, is_valid_byte))
        break;
      m->count.data++;
      return true;
    default:
      break;
  }
  *buf = start_buf;
  return false;
}

bool is_valid_elemkind(const uint8_t **buf, size_t len, module_t *m)
{
  if (len < 1 || (*buf)[0] != 0x00)
    return false;
  (*buf)++;
  m->count.elemkind++;
  return true;
}

bool is_valid_elem(const uint8_t **buf, size_t len, module_t *m)
{
  const uint8_t *start_buf = *buf;
  uint32_t op;
  if (!leb_u32(buf, LEN, &op)) {
    *buf = start_buf;
    return false;
  }
  switch (op) {
    case 0:
      if (!is_valid_expr(buf, LEN, m) ||
          !is_valid_vec(buf, LEN, m, is_valid_funcidx))
        break;
      m->count.elem++;
      return true;
    case 1:
      if (!is_valid_elemkind(buf, LEN, m) ||
          !is_valid_vec(buf, LEN, m, is_valid_funcidx))
        break;
      m->count.elem++;
      return true;
    case 2:
      if (!is_valid_tableidx(buf, LEN, m) ||
          !is_valid_expr(buf, LEN, m)     ||
          !is_valid_elemkind(buf, LEN, m) ||
          !is_valid_vec(buf, LEN, m, is_valid_funcidx))
        break;
      m->count.elem++;
      return true;
    case 3:
      if (!is_valid_elemkind(buf, LEN, m) ||
          !is_valid_vec(buf, LEN, m, is_valid_funcidx))
        break;
      m->count.elem++;
      return true;
    case 4:
      if (!is_valid_expr(buf, LEN, m) ||
          !is_valid_vec(buf, LEN, m, is_valid_expr))
        break;
      m->count.elem++;
      return true;
    case 5:
      if (!is_valid_reftype(buf, LEN, m) ||
          !is_valid_vec(buf, LEN, m, is_valid_expr))
        break;
      m->count.elem++;
      return true;
    case 6:
      if (!is_valid_tableidx(buf, LEN, m) ||
          !is_valid_expr(buf, LEN, m)     ||
          !is_valid_reftype(buf, LEN, m)  ||
          !is_valid_vec(buf, LEN, m, is_valid_expr))
        break;
      m->count.elem++;
      return true;
    case 7:
      if (!is_valid_reftype(buf, LEN, m) ||
          !is_valid_vec(buf, LEN, m, is_valid_expr))
        break;
      m->count.elem++;
      return true;
    default:
      break;
  }
  *buf = start_buf;
  return false;
}

bool is_valid_section(size_t num, const uint8_t **buf, size_t len, module_t *m)
{
  uint32_t n;
  if (*buf == NULL) {
    return true;
  }
  switch(num) {
    case 1:  return is_valid_vec(buf, len, m, is_valid_functype);
    case 2:  return is_valid_vec(buf, len, m, is_valid_import);
    case 3:  return is_valid_vec(buf, len, m, is_valid_func);
    case 4:  return is_valid_vec(buf, len, m, is_valid_tabletype);
    case 5:  return is_valid_vec(buf, len, m, is_valid_memtype);
    case 6:  return is_valid_vec(buf, len, m, is_valid_global);
    case 7:  return is_valid_vec(buf, len, m, is_valid_export);
    case 8:  return is_valid_funcidx(buf, len, m);
    case 9:  return is_valid_vec(buf, len, m, is_valid_elem);
    case 10: return is_valid_vec(buf, len, m, is_valid_code);
    case 11: return is_valid_vec(buf, len, m, is_valid_data);
    case 12: return leb_u32(buf, len, &n); /* number of data setments in the data section. */
    default:
      break;
  }
  return false;
}

bool is_valid_wasm(const uint8_t *start_buf, size_t len, module_t *m)
{
  struct module_st    module_tmp       = { 0 };
  const uint8_t      *bufptr           = start_buf;
  const uint8_t     **buf              = &bufptr;
  unsigned char       magic_check[4]   = { '\0', 'a', 's', 'm' };
  unsigned char       version_check[4] = { 1, 0, 0, 0 };
  size_t              i                = 0;
  size_t              sections_n       = sizeof(module_tmp.section) / sizeof(module_tmp.section[0]);

  if (m == NULL) {
    m = &module_tmp;
  }

  if (len < 8) {
    DEBUG("Buflen too small");
    return false;
  }

  if (memcmp(*buf, magic_check, 4) != 0) {
    DEBUG("Bad magic number\n");
    goto invalid;
  }
  (*buf) += 4;

  if (memcmp(*buf, version_check, 4) != 0) {
    DEBUG("Bad version number\n");
    goto invalid;
  }
  (*buf) += 4;

  while (LEN > 0) {
    uint8_t  section_type = (*buf)[0];
    uint64_t section_len;
    (*buf)++;
    if (section_type >= sections_n     ||
        !leb_u64(buf, LEN, &section_len))
      goto invalid;
    if (section_type == 0) {
      /* Ignore comment sections */
    } else {
      if (m->section[section_type].buf != NULL) {
        DEBUG("Duplicate sections currently unhandled\n");
        goto invalid;
      }
      m->section[section_type].buf = *buf;
      m->section[section_type].len = section_len;

      *buf = m->section[section_type].buf;
    }
    (*buf) += section_len;
  }
  if (LEN != 0) {
    DEBUG("Bad section alignment\n");
    goto invalid;
  }

  for(i=0; i<sections_n; i++) {
    const uint8_t *bufptr = m->section[i].buf;
    DEBUG("sections[%zu] = { buf: %p, len: %zu }\n", i, m->section[i].buf, m->section[i].len);
    if (!is_valid_section(i, &bufptr, m->section[i].len, m)) {
      DEBUG("Invalid Section: %d\n", i);
      return "Invalid section";
    }
  }
#if DEBUG_VALIDATE
  print_counts(m);
#endif
  return true;
invalid:
  *buf = start_buf;
  return false;
}
