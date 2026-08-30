#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "instr.h"
#include "leb128.h"

op_t instr_memarg(const uint8_t **buf, op_param_t *param, op_t op)
{
  uint32_t u32;
  leb_u32(buf, 5, &u32);
  param->u.memarg.a = u32;
  leb_u32(buf, 5, &u32);
  param->u.memarg.o = u32;
  return op;
}

op_t instr_u32u32(const uint8_t **buf, op_param_t *param, op_t op)
{
  uint32_t u32;
  leb_u32(buf, 5, &u32);
  param->u.u32u32.u32_1 = u32;
  leb_u32(buf, 5, &u32);
  param->u.u32u32.u32_2 = u32;
  return op;
}

op_t instr_u32(const uint8_t **buf, op_param_t *param, op_t op)
{
  uint32_t u32;
  leb_u32(buf, 5, &u32);
  param->u.u32 = u32;
  return op;
}

op_t instr_i32(const uint8_t **buf, op_param_t *param, op_t op)
{
  int32_t i32;
  leb_i32(buf, 5, &i32);
  param->u.u32 = i32;
  return op;
}

op_t instr_i64(const uint8_t **buf, op_param_t *param, op_t op)
{
  int64_t i64;
  memset(param, 0, sizeof(*param));
  leb_i64(buf, 10, &i64);
  param->u.i64 = i64;
  return op;
}

op_t instr_dataidx_0(const uint8_t **buf, op_param_t *param, op_t op)
{
  instr_u32(buf, param, op);
  assert((*buf)[0] == 0x00);
  (*buf)++;
  return op;
}

op_t instr_0(const uint8_t **buf, op_param_t *param, op_t op)
{
  assert((*buf)[0] == 0x00);
  (*buf)++;
  return op;
}

op_t instr_0_0(const uint8_t **buf, op_param_t *param, op_t op)
{
  assert((*buf)[0] == 0x00);
  (*buf)++;
  assert((*buf)[0] == 0x00);
  (*buf)++;
  return op;
}

op_t instr_bytes(const uint8_t **buf, op_param_t *param, op_t op, uint32_t len)
{
  param->u.bytes.len = len;
  param->u.bytes.buf = (*buf);
  (*buf) += len;
  return op;
}

op_t instr_vecbytes(const uint8_t **buf, op_param_t *param, op_t op)
{
  uint32_t u32;
  leb_u32(buf, 5, &u32);
  param->u.bytes.len = u32;
  param->u.bytes.buf = (*buf);
  (*buf) += u32;
  return op;
}

op_t instr_br_table(const uint8_t **buf, op_param_t *param, op_t op)
{
  uint32_t u32;
  uint32_t u32_tmp;
  printf(">instr_br_table\n");
  leb_u32(buf, 5, &u32);
  printf("u32: %u\n", u32);
  u32++; /* To account for final label_N */
  param->u.bytes.len = u32;
  param->u.bytes.buf = (*buf); /* Note that these are u32 and not bytes */
  for (; u32-->0;) {
    if (leb_u32(buf, 5, &u32_tmp)) {
      printf("u32_tmp: %u\n", u32_tmp);
    }
  }
  printf("<instr_br_table\n");
  return op;
}

op_t instr_blocktype(const uint8_t **buf, op_param_t *param, op_t op)
{
  int      mode = 0;
  uint8_t  byte = 0;
  union {
    uint32_t u32;
    uint8_t  s[5];
  } u;
  byte = (*buf)[0];
  (*buf)++;
  switch (byte) {
    case 0x40:
      param->u.block.type = BLOCKTYPE_EPSILON;
      return op;
    case 0x7F:
    case 0x7E:
    case 0x7D:
    case 0x7C:
    case 0x7B:
    case 0x70:
    case 0x6F:
      param->u.block.type = BLOCKTYPE_VALTYPE;
      param->u.block.u.t  = byte;
      return op;
    default:
      break;
  }
  (*buf)--;
  param->u.block.type = BLOCKTYPE_X;
  leb_s(33, buf, 5, u.s, 5);
  param->u.block.u.x = u.u32;
  return op;
}


op_t instr_next(const uint8_t **buf, op_param_t *param)
{
  const uint8_t     *start_buf = *buf;
  uint8_t            byte      = 0;
  uint8_t            op        = 0;
  uint32_t           subop     = 0;
  int32_t            i32       = 0;
  int64_t            i64       = 0;
  float              f32       = 0;
  double             f64       = 0;
  struct op_param_st param_tmp = { 0 };

  if (param == NULL) {
    param = &param_tmp;
  }

  op = (*buf)[0];
  (*buf)++;
  switch (op) {
    case 0x0B: return OOP_END;
    case 0x05: return OOP_ELSE;
    case 0x00: return OP_UNREACHABLE;
    case 0x01: return OP_NOP;
    case 0x0F: return OP_RETURN;
    case 0x1A: return OP_DROP;
    case 0x1B: return OP_SELECT;
    case 0x45: return OP_I32_EQZ;
    case 0x46: return OP_I32_EQ;
    case 0x47: return OP_I32_NE;
    case 0x48: return OP_I32_LT_S;
    case 0x49: return OP_I32_LT_U;
    case 0x4A: return OP_I32_GT_S;
    case 0x4B: return OP_I32_GT_U;
    case 0x4C: return OP_I32_LE_S;
    case 0x4D: return OP_I32_LE_U;
    case 0x4E: return OP_I32_GE_S;
    case 0x4F: return OP_I32_GE_U;
    case 0x50: return OP_I64_EQZ;
    case 0x51: return OP_I64_EQ;
    case 0x52: return OP_I64_NE;
    case 0x53: return OP_I64_LT_S;
    case 0x54: return OP_I64_LT_U;
    case 0x55: return OP_I64_GT_S;
    case 0x56: return OP_I64_GT_U;
    case 0x57: return OP_I64_LE_S;
    case 0x58: return OP_I64_LE_U;
    case 0x59: return OP_I64_GE_S;
    case 0x5A: return OP_I64_GE_U;
    case 0x5B: return OP_F32_EQ;
    case 0x5C: return OP_F32_NE;
    case 0x5D: return OP_F32_LT;
    case 0x5E: return OP_F32_GT;
    case 0x5F: return OP_F32_LE;
    case 0x60: return OP_F32_GE;
    case 0x61: return OP_F64_EQ;
    case 0x62: return OP_F64_NE;
    case 0x63: return OP_F64_LT;
    case 0x64: return OP_F64_GT;
    case 0x65: return OP_F64_LE;
    case 0x66: return OP_F64_GE;
    case 0x67: return OP_I32_CLZ;
    case 0x68: return OP_I32_CTZ;
    case 0x69: return OP_I32_POPCNT;
    case 0x6A: return OP_I32_ADD;
    case 0x6B: return OP_I32_SUB;
    case 0x6C: return OP_I32_MUL;
    case 0x6D: return OP_I32_DIV_S;
    case 0x6E: return OP_I32_DIV_U;
    case 0x6F: return OP_I32_REM_S;
    case 0x70: return OP_I32_REM_U;
    case 0x71: return OP_I32_AND;
    case 0x72: return OP_I32_OR;
    case 0x73: return OP_I32_XOR;
    case 0x74: return OP_I32_SHL;
    case 0x75: return OP_I32_SHR_S;
    case 0x76: return OP_I32_SHR_U;
    case 0x77: return OP_I32_ROTL;
    case 0x78: return OP_I32_ROTR;
    case 0x79: return OP_I64_CLZ;
    case 0x7A: return OP_I64_CTZ;
    case 0x7B: return OP_I64_POPCNT;
    case 0x7C: return OP_I64_ADD;
    case 0x7D: return OP_I64_SUB;
    case 0x7E: return OP_I64_MUL;
    case 0x7F: return OP_I64_DIV_S;
    case 0x80: return OP_I64_DIV_U;
    case 0x81: return OP_I64_REM_S;
    case 0x82: return OP_I64_REM_U;
    case 0x83: return OP_I64_AND;
    case 0x84: return OP_I64_OR;
    case 0x85: return OP_I64_XOR;
    case 0x86: return OP_I64_SHL;
    case 0x87: return OP_I64_SHR_S;
    case 0x88: return OP_I64_SHR_U;
    case 0x89: return OP_I64_ROTL;
    case 0x8A: return OP_I64_ROTR;
    case 0x8B: return OP_F32_ABS;
    case 0x8C: return OP_F32_NEG;
    case 0x8D: return OP_F32_CEIL;
    case 0x8E: return OP_F32_FLOOR;
    case 0x8F: return OP_F32_TRUNC;
    case 0x90: return OP_F32_NEAREST;
    case 0x91: return OP_F32_SQRT;
    case 0x92: return OP_F32_ADD;
    case 0x93: return OP_F32_SUB;
    case 0x94: return OP_F32_MUL;
    case 0x95: return OP_F32_DIV;
    case 0x96: return OP_F32_MIN;
    case 0x97: return OP_F32_MAX;
    case 0x98: return OP_F32_COPYSIGN;
    case 0x99: return OP_F64_ABS;
    case 0x9A: return OP_F64_NEG;
    case 0x9B: return OP_F64_CEIL;
    case 0x9C: return OP_F64_FLOOR;
    case 0x9D: return OP_F64_TRUNC;
    case 0x9E: return OP_F64_NEAREST;
    case 0x9F: return OP_F64_SQRT;
    case 0xA0: return OP_F64_ADD;
    case 0xA1: return OP_F64_SUB;
    case 0xA2: return OP_F64_MUL;
    case 0xA3: return OP_F64_DIV;
    case 0xA4: return OP_F64_MIN;
    case 0xA5: return OP_F64_MAX;
    case 0xA6: return OP_F64_COPYSIGN;
    case 0xA7: return OP_I32_WRAP_I64;
    case 0xA8: return OP_I32_TRUNC_F32_S;
    case 0xA9: return OP_I32_TRUNC_F32_U;
    case 0xAA: return OP_I32_TRUNC_F64_S;
    case 0xAB: return OP_I32_TRUNC_F64_U;
    case 0xAC: return OP_I64_EXTEND_I32_S;
    case 0xAD: return OP_I64_EXTEND_I32_U;
    case 0xAE: return OP_I64_TRUNC_F32_S;
    case 0xAF: return OP_I64_TRUNC_F32_U;
    case 0xB0: return OP_I64_TRUNC_F64_S;
    case 0xB1: return OP_I64_TRUNC_F64_U;
    case 0xB2: return OP_F32_CONVERT_I32_S;
    case 0xB3: return OP_F32_CONVERT_I32_U;
    case 0xB4: return OP_F32_CONVERT_I64_S;
    case 0xB5: return OP_F32_CONVERT_I64_U;
    case 0xB6: return OP_F32_DEMOTE_F64;
    case 0xB7: return OP_F64_CONVERT_I32_S;
    case 0xB8: return OP_F64_CONVERT_I32_U;
    case 0xB9: return OP_F64_CONVERT_I64_S;
    case 0xBA: return OP_F64_CONVERT_I64_U;
    case 0xBB: return OP_F64_PROMOTE_F32;
    case 0xBC: return OP_I32_REINTERPRET_F32;
    case 0xBD: return OP_I64_REINTERPRET_F64;
    case 0xBE: return OP_F32_REINTERPRET_I32;
    case 0xBF: return OP_F64_REINTERPRET_I64;
    case 0xC0: return OP_I32_EXTEND8_S;
    case 0xC1: return OP_I32_EXTEND16_S;
    case 0xC2: return OP_I64_EXTEND8_S;
    case 0xC3: return OP_I64_EXTEND16_S;
    case 0xC4: return OP_I64_EXTEND32_S;
    case 0x28: return instr_memarg   (buf, param, OP_I32_LOAD_M);
    case 0x29: return instr_memarg   (buf, param, OP_I64_LOAD_M);
    case 0x2A: return instr_memarg   (buf, param, OP_F32_LOAD_M);
    case 0x2B: return instr_memarg   (buf, param, OP_F64_LOAD_M);
    case 0x2C: return instr_memarg   (buf, param, OP_I32_LOAD8_S_M);
    case 0x2D: return instr_memarg   (buf, param, OP_I32_LOAD8_U_M);
    case 0x2E: return instr_memarg   (buf, param, OP_I32_LOAD16_S_M);
    case 0x2F: return instr_memarg   (buf, param, OP_I32_LOAD16_U_M);
    case 0x30: return instr_memarg   (buf, param, OP_I64_LOAD8_S_M);
    case 0x31: return instr_memarg   (buf, param, OP_I64_LOAD8_U_M);
    case 0x32: return instr_memarg   (buf, param, OP_I64_LOAD16_S_M);
    case 0x33: return instr_memarg   (buf, param, OP_I64_LOAD16_U_M);
    case 0x34: return instr_memarg   (buf, param, OP_I64_LOAD32_S_M);
    case 0x35: return instr_memarg   (buf, param, OP_I64_LOAD32_U_M);
    case 0x36: return instr_memarg   (buf, param, OP_I32_STORE_M);
    case 0x37: return instr_memarg   (buf, param, OP_I64_STORE_M);
    case 0x38: return instr_memarg   (buf, param, OP_F32_STORE_M);
    case 0x39: return instr_memarg   (buf, param, OP_F64_STORE_M);
    case 0x3A: return instr_memarg   (buf, param, OP_I32_STORE8_M);
    case 0x3B: return instr_memarg   (buf, param, OP_I32_STORE16_M);
    case 0x3C: return instr_memarg   (buf, param, OP_I64_STORE8_M);
    case 0x3D: return instr_memarg   (buf, param, OP_I64_STORE16_M);
    case 0x3E: return instr_memarg   (buf, param, OP_I64_STORE32_M);
    case 0x20: return instr_u32      (buf, param, OP_LOCAL_GET_X);
    case 0x21: return instr_u32      (buf, param, OP_LOCAL_SET_X);
    case 0x22: return instr_u32      (buf, param, OP_LOCAL_TEE_X);
    case 0x02: return instr_blocktype(buf, param, OP_BLOCK);
    case 0x03: return instr_blocktype(buf, param, OP_LOOP);
    case 0x04: return instr_blocktype(buf, param, OP_IF);
    case 0x0C: return instr_u32      (buf, param, OP_BR_L);
    case 0x0D: return instr_u32      (buf, param, OP_BR_IF_L);
    case 0x0E: return instr_br_table (buf, param, OP_BR_TABLE);
    case 0x10: return instr_u32      (buf, param, OP_CALL_X);
    case 0x11: return instr_u32u32   (buf, param, OP_CALL_INDIRECT_X_Y);
    case 0x1C: return instr_vecbytes (buf, param, OP_SELECT_T);
    case 0x23: return instr_u32      (buf, param, OP_GLOBAL_GET_X);
    case 0x24: return instr_u32      (buf, param, OP_GLOBAL_SET_X);
    case 0x25: return instr_u32      (buf, param, OP_TABLE_GET_X);
    case 0x26: return instr_u32      (buf, param, OP_TABLE_SET_X);
    case 0x3F: return instr_0        (buf, param, OP_MEMORY_SIZE);
    case 0x40: return instr_0        (buf, param, OP_MEMORY_GROW);
    case 0x41: return instr_i32      (buf, param, OP_I32_CONST_N);
    case 0x42: return instr_i64      (buf, param, OP_I64_CONST_N);
    case 0x43: return instr_bytes    (buf, param, OP_F32_CONST_Z, 4);
    case 0x44: return instr_bytes    (buf, param, OP_F64_CONST_Z, 8);
    case 0xFD:
      leb_u32(buf, 5, &subop);
      switch (subop) {
        case  0: return instr_memarg (buf, param, OP_V128_LOAD_M);
        case  1: return instr_memarg (buf, param, OP_V128_LOAD8X8_S_M);
        case  2: return instr_memarg (buf, param, OP_V128_LOAD8X8_U_M);
        case  3: return instr_memarg (buf, param, OP_V128_LOAD16X4_S_M);
        case  4: return instr_memarg (buf, param, OP_V128_LOAD16X4_U_M);
        case  5: return instr_memarg (buf, param, OP_V128_LOAD32X2_S_M);
        case  6: return instr_memarg (buf, param, OP_V128_LOAD32X2_U_M);
        case  7: return instr_memarg (buf, param, OP_V128_LOAD8_SPLAT_M);
        case  8: return instr_memarg (buf, param, OP_V128_LOAD16_SPLAT_M);
        case  9: return instr_memarg (buf, param, OP_V128_LOAD32_SPLAT_M);
        case 10: return instr_memarg (buf, param, OP_V128_LOAD64_SPLAT_M);
        case 92: return instr_memarg (buf, param, OP_V128_LOAD32_ZERO_M);
        case 93: return instr_memarg (buf, param, OP_V128_LOAD64_ZERO_M);
        case 11: return instr_memarg (buf, param, OP_V128_STORE_M);
	case 84: return instr_u32u32 (buf, param, OP_V128_LOAD8_LANE_M_L);
	case 85: return instr_u32u32 (buf, param, OP_V128_LOAD16_LANE_M_L);
	case 86: return instr_u32u32 (buf, param, OP_V128_LOAD32_LANE_M_L);
	case 87: return instr_u32u32 (buf, param, OP_V128_LOAD64_LANE_M_L);
	case 88: return instr_u32u32 (buf, param, OP_V128_STORE8_LANE_M_L);
	case 89: return instr_u32u32 (buf, param, OP_V128_STORE16_LANE_M_L);
        case 90: return instr_u32u32 (buf, param, OP_V128_STORE32_LANE_M_L);
        case 91: return instr_u32u32 (buf, param, OP_V128_STORE64_LANE_M_L);
	case 12: return instr_bytes  (buf, param, OP_V128_CONST_V, 16);
	case 13: return instr_bytes  (buf, param, OP_I8X16_SHUFFLE, 16);
/* i8x16.shuffle l^{16} spec calls for laneidx (u32), but indices should be [0,15] or [16,31] (if specifying i - 16).
 * Thus bytes instead of leb, because for vals < 32 leb == byte */
	case  21: return instr_u32   (buf, param, OP_I8X16_EXTRACT_LANE_S_L);
	case  22: return instr_u32   (buf, param, OP_I8X16_EXTRACT_LANE_U_L);
	case  23: return instr_u32   (buf, param, OP_I8X16_REPLACE_LANE_L);
	case  24: return instr_u32   (buf, param, OP_I16X8_EXTRACT_LANE_S_L);
	case  25: return instr_u32   (buf, param, OP_I16X8_EXTRACT_LANE_U_L);
	case  26: return instr_u32   (buf, param, OP_I16X8_REPLACE_LANE_L);
	case  27: return instr_u32   (buf, param, OP_I32X4_EXTRACT_LANE_L);
	case  28: return instr_u32   (buf, param, OP_I32X4_REPLACE_LANE_L);
	case  29: return instr_u32   (buf, param, OP_I64X2_EXTRACT_LANE_L);
	case  30: return instr_u32   (buf, param, OP_I64X2_REPLACE_LANE_L);
	case  31: return instr_u32   (buf, param, OP_F32X4_EXTRACT_LANE_L);
	case  32: return instr_u32   (buf, param, OP_F32X4_REPLACE_LANE_L);
	case  33: return instr_u32   (buf, param, OP_F64X2_EXTRACT_LANE_L);
	case  34: return instr_u32   (buf, param, OP_F64X2_REPLACE_LANE_L);
	case  14: return OP_I8X16_SWIZZLE;
	case  15: return OP_I8X16_SPLAT;
	case  16: return OP_I16X8_SPLAT;
	case  17: return OP_I32X4_SPLAT;
	case  18: return OP_I64X2_SPLAT;
	case  19: return OP_F32X4_SPLAT;
	case  20: return OP_F64X2_SPLAT;
	case  35: return OP_I8X16_EQ;
	case  36: return OP_I8X16_NE;
	case  37: return OP_I8X16_LT_S;
	case  38: return OP_I8X16_LT_U;
	case  39: return OP_I8X16_GT_S;
	case  40: return OP_I8X16_GT_U;
	case  41: return OP_I8X16_LE_S;
	case  42: return OP_I8X16_LE_U;
	case  43: return OP_I8X16_GE_S;
	case  44: return OP_I8X16_GE_U;
	case  45: return OP_I16X8_EQ;
	case  46: return OP_I16X8_NE;
	case  47: return OP_I16X8_LT_S;
	case  48: return OP_I16X8_LT_U;
	case  49: return OP_I16X8_GT_S;
	case  50: return OP_I16X8_GT_U;
	case  51: return OP_I16X8_LE_S;
	case  52: return OP_I16X8_LE_U;
	case  53: return OP_I16X8_GE_S;
	case  54: return OP_I16X8_GE_U;
	case  55: return OP_I32X4_EQ;
	case  56: return OP_I32X4_NE;
	case  57: return OP_I32X4_LT_S;
	case  58: return OP_I32X4_LT_U;
	case  59: return OP_I32X4_GT_S;
	case  60: return OP_I32X4_GT_U;
	case  61: return OP_I32X4_LE_S;
	case  62: return OP_I32X4_LE_U;
	case  63: return OP_I32X4_GE_S;
	case  64: return OP_I32X4_GE_U;
	case 214: return OP_I64X2_EQ;
	case 215: return OP_I64X2_NE;
	case 216: return OP_I64X2_LT_S;
	case 217: return OP_I64X2_GT_S;
	case 218: return OP_I64X2_LE_S;
	case 219: return OP_I64X2_GE_S;
	case  65: return OP_F32X4_EQ;
	case  66: return OP_F32X4_NE;
	case  67: return OP_F32X4_LT;
	case  68: return OP_F32X4_GT;
	case  69: return OP_F32X4_LE;
	case  70: return OP_F32X4_GE;
	case  71: return OP_F64X2_EQ;
	case  72: return OP_F64X2_NE;
	case  73: return OP_F64X2_LT;
	case  74: return OP_F64X2_GT;
	case  75: return OP_F64X2_LE;
	case  76: return OP_F64X2_GE;
	case  77: return OP_V128_NOT;
	case  78: return OP_V128_AND;
	case  79: return OP_V128_ANDNOT;
	case  80: return OP_V128_OR;
	case  81: return OP_V128_XOR;
	case  82: return OP_V128_BITSELECT;
	case  83: return OP_V128_ANY_TRUE;
	case  96: return OP_I8X16_ABS;
	case  97: return OP_I8X16_NEG;
	case  98: return OP_I8X16_POPCNT;
	case  99: return OP_I8X16_ALL_TRUE;
	case 100: return OP_I8X16_BITMASK;
	case 101: return OP_I8X16_NARROW_I16X8_S;
	case 102: return OP_I8X16_NARROW_I16X8_U;
	case 107: return OP_I8X16_SHL;
	case 108: return OP_I8X16_SHR_S;
	case 109: return OP_I8X16_SHL_U;
	case 110: return OP_I8X16_ADD;
	case 111: return OP_I8X16_ADD_SAT_S;
	case 112: return OP_I8X16_ADD_SAT_U;
	case 113: return OP_I8X16_SUB;
	case 114: return OP_I8X16_SUB_SAT_S;
	case 115: return OP_I8X16_SUB_SAT_U;
	case 118: return OP_I8X16_MIN_S;
	case 119: return OP_I8X16_MIN_U;
	case 120: return OP_I8X16_MAX_S;
	case 121: return OP_I8X16_MAX_U;
	case 123: return OP_I8X16_AVGR_U;
        case 124: return OP_I16X8_EXTADD_PAIRWISE_I8X16_S;
        case 125: return OP_I16X8_EXTADD_PAIRWISE_I8X16_U;
        case 128: return OP_I16X8_ABS;
        case 129: return OP_I16X8_NEG;
        case 130: return OP_I16X8_Q15MULR_SAT_S;
        case 131: return OP_I16X8_ALL_TRUE;
        case 132: return OP_I16X8_BITMASK;
        case 133: return OP_I16X8_NARROW_I32X4_S;
        case 134: return OP_I16X8_NARROW_I32X4_U;
        case 135: return OP_I16X8_EXTEND_LOW_I8X16_S;
        case 136: return OP_I16X8_EXTEND_HIGH_I8X16_S;
        case 137: return OP_I16X8_EXTEND_LOW_I8X16_U;
        case 138: return OP_I16X8_EXTEND_HIGH_I8X16_U;
        case 139: return OP_I16X8_SHL;
        case 140: return OP_I16X8_SHR_S;
        case 141: return OP_I16X8_SHR_U;
        case 142: return OP_I16X8_ADD;
        case 143: return OP_I16X8_ADD_SAT_S;
        case 144: return OP_I16X8_ADD_SAT_U;
        case 145: return OP_I16X8_SUB;
        case 146: return OP_I16X8_SUB_SAT_S;
        case 147: return OP_I16X8_SUB_SAT_U;
        case 149: return OP_I16X8_MUL;
        case 150: return OP_I16X8_MIN_S;
        case 151: return OP_I16X8_MIN_U;
        case 152: return OP_I16X8_MAX_S;
        case 153: return OP_I16X8_MAX_U;
        case 155: return OP_I16X8_AVGR_U;
        case 156: return OP_I16X8_EXTMUL_LOW_I8X16_S;
        case 157: return OP_I16X8_EXTMUL_HIGH_I8X16_S;
        case 158: return OP_I16X8_EXTMUL_LOW_I8X16_U;
        case 159: return OP_I16X8_EXTMUL_HIGH_I8X16_U;
	case 126: return OP_I32X4_EXTADD_PAIRWISE_I16X8_S;
	case 127: return OP_I32X4_EXTADD_PAIRWISE_I16X8_U;
	case 160: return OP_I32X4_ABS;
	case 161: return OP_I32X4_NEG;
	case 163: return OP_I32X4_ALL_TRUE;
	case 164: return OP_I32X4_BITMASK;
	case 167: return OP_I32X4_EXTEND_LOW_I16X8_S;
	case 168: return OP_I32X4_EXTEND_HIGH_I16X8_S;
	case 169: return OP_I32X4_EXTEND_LOW_I16X8_U;
	case 170: return OP_I32X4_EXTEND_HIGH_I16X8_U;
	case 171: return OP_I32X4_SHL;
	case 172: return OP_I32X4_SHR_S;
	case 173: return OP_I32X4_SHR_U;
	case 174: return OP_I32X4_ADD;
	case 177: return OP_I32X4_SUB;
	case 181: return OP_I32X4_MUL;
	case 182: return OP_I32X4_MIN_S;
	case 183: return OP_I32X4_MIN_U;
	case 184: return OP_I32X4_MAX_S;
	case 185: return OP_I32X4_MAX_U;
	case 186: return OP_I32X4_DOT_I16X8_S;
	case 188: return OP_I32X4_EXTMUL_LOW_I16X8_S;
	case 189: return OP_I32X4_EXTMUL_HIGH_I16X8_S;
	case 190: return OP_I32X4_EXTMUL_LOW_I16X8_U;
	case 191: return OP_I32X4_EXTMUL_HIGH_I16X8_U;
	case 192: return OP_I64X2_ABS;
	case 193: return OP_I64X2_NEG;
	case 195: return OP_I64X2_ALL_TRUE;
	case 196: return OP_I64X2_BITMASK;
	case 199: return OP_I64X2_EXTEND_LOW_I32X4_S;
	case 200: return OP_I64X2_EXTEND_HIGH_I32X4_S;
	case 201: return OP_I64X2_EXTEND_LOW_I32X4_U;
	case 202: return OP_I64X2_EXTEND_HIGH_I32X4_U;
	case 203: return OP_I64X2_SHL;
	case 204: return OP_I64X2_SHR_S;
	case 205: return OP_I64X2_SHR_U;
	case 206: return OP_I64X2_ADD;
	case 209: return OP_I64X2_SUB;
	case 213: return OP_I64X2_MUL;
	case 220: return OP_I64X2_EXTMUL_LOW_I32X4_S;
	case 221: return OP_I64X2_EXTMUL_HIGH_I32X4_S;
	case 222: return OP_I64X2_EXTMUL_LOW_I32X4_U;
	case 223: return OP_I64X2_EXTMUL_HIGH_I32X4_U;
	case 103: return OP_F32X4_CEIL;
	case 104: return OP_F32X4_FLOOR;
	case 105: return OP_F32X4_TRUNC;
	case 106: return OP_F32X4_NEAREST;
	case 224: return OP_F32X4_ABS;
	case 225: return OP_F32X4_NEG;
	case 227: return OP_F32X4_SQRT;
	case 228: return OP_F32X4_ADD;
	case 229: return OP_F32X4_SUB;
	case 230: return OP_F32X4_MUL;
	case 231: return OP_F32X4_DIV;
	case 232: return OP_F32X4_MIN;
	case 233: return OP_F32X4_MAX;
	case 234: return OP_F32X4_PMIN;
	case 235: return OP_F32X4_PMAX;
	case 116: return OP_F64X2_CEIL;
	case 117: return OP_F64X2_FLOOR;
	case 122: return OP_F64X2_TRUNC;
	case 148: return OP_F64X2_NEAREST;
	case 236: return OP_F64X2_ABS;
	case 237: return OP_F64X2_NEG;
	case 239: return OP_F64X2_SQRT;
	case 240: return OP_F64X2_ADD;
	case 241: return OP_F64X2_SUB;
	case 242: return OP_F64X2_MUL;
	case 243: return OP_F64X2_DIV;
	case 244: return OP_F64X2_MIN;
	case 245: return OP_F64X2_MAX;
	case 246: return OP_F64X2_PMIN;
	case 247: return OP_F64X2_PMAX;
        case 248: return OP_I32X4_TRUNC_SAT_F32X4_S;
        case 249: return OP_I32X4_TRUNC_SAT_F32X4_U;
        case 250: return OP_F32X4_CONVERT_I32X4_S;
        case 251: return OP_F32X4_CONVERT_I32X4_U;
        case 252: return OP_I32X4_TRUNC_SAT_F64X2_S_ZERO;
        case 253: return OP_I32X4_TRUNC_SAT_F64X2_U_ZERO;
        case 254: return OP_F64X2_CONVERT_LOW_I32X4_S;
        case 255: return OP_F64X2_CONVERT_LOW_I32X4_U;
        case  94: return OP_F32X4_DEMOTE_F64X2_ZERO;
        case  95: return OP_F64X2_PROMOTE_LOW_F32X4;
	default:
          assert(false);
      }
      break;
    case 0xFC:
      leb_u32(buf, 5, &subop);
      switch (subop) {
        case  0: return OP_I32_TRUNC_SAT_F32_S;
        case  1: return OP_I32_TRUNC_SAT_F32_U;
        case  2: return OP_I32_TRUNC_SAT_F32_S;
        case  3: return OP_I32_TRUNC_SAT_F32_U;
        case  4: return OP_I64_TRUNC_SAT_F32_S;
        case  5: return OP_I64_TRUNC_SAT_F32_U;
        case  6: return OP_I64_TRUNC_SAT_F32_S;
        case  7: return OP_I64_TRUNC_SAT_F32_U;
        case  8: return instr_dataidx_0(buf, param, OP_MEMORY_INIT_X);
	case  9: return instr_u32      (buf, param, OP_DATA_DROP_X);
	case 10: return instr_0_0      (buf, param, OP_MEMORY_COPY);
	case 11: return instr_0        (buf, param, OP_MEMORY_FILL);
        case 12: return instr_u32u32   (buf, param, OP_TABLE_INIT_X_Y);
        case 14: return instr_u32u32   (buf, param, OP_TABLE_COPY_X_Y);
	case 13: return instr_u32      (buf, param, OP_ELEM_DROP_X);
        case 15: return instr_u32      (buf, param, OP_TABLE_GROW_X);
        case 16: return instr_u32      (buf, param, OP_TABLE_SIZE_X);
        case 17: return instr_u32      (buf, param, OP_TABLE_FILL_X);
	default:
	  assert(false);
      }
      assert(false);
    default:
      assert(false);
  }
  assert(false);
}
