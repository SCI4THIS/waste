#ifndef FILEGUARD_DIS_H__
#define FILEGUARD_DIS_H__

typedef uint8_t valtype_t;

typedef struct locals_st {
  uint32_t  n;
  valtype_t t;
} locals_t;

const char *dis_reftype   (uint8_t type);
const char *dis_valtype   (uint8_t type);
const char *dis_exportdesc(uint8_t type);
const char *dis_locals    (locals_t *locals, size_t len, char *buf, size_t buflen);
void        dis_expr      (const uint8_t *buf, size_t len, bool is_newline);

#endif
