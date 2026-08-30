#ifndef FILEGUARD_VALIDATE_H__
#define FILEGUARD_VALIDATE_H__

#include "leb128.h"

typedef struct section_count_st {
  uint32_t numtype;
  uint32_t vectype;
  uint32_t reftype;
  uint32_t valtype;
  uint32_t functype;
  uint32_t name;
  uint32_t mut;
  uint32_t globaltype;
  uint32_t memtype;
  uint32_t tabletype;
  uint32_t typeidx;
  uint32_t importdesc;
  uint32_t import;
  uint32_t blocktype;
  uint32_t tableidx;
  uint32_t funcidx;
  uint32_t localidx;
  uint32_t globalidx;
  uint32_t labelidx;
  uint32_t elemidx;
  uint32_t memidx;
  uint32_t dataidx;
  uint32_t laneidx;
  uint32_t memarg;
  uint32_t expr;
  uint32_t exportdesc;
  uint32_t export;
  uint32_t global;
  uint32_t locals;
  uint32_t code;
  uint32_t data;
  uint32_t elem;
  uint32_t elemkind;
  uint32_t limits;
  uint32_t instr;
  uint32_t resulttype;
  uint32_t func;
} section_count_t;

typedef struct module_st {
  struct section_count_st    count;
  struct {
    const uint8_t           *buf;
    size_t                   len;
  } section[13];
} module_t;

typedef bool (*is_ending_t) (const uint8_t byte);
typedef bool (*is_validator_t)(const uint8_t **buf, size_t len, module_t *m);

bool is_valid_vec   (const uint8_t **buf, size_t len, module_t *m, is_validator_t is_validator);
bool is_ending_0x0B (const uint8_t byte);

/* module_t m can be NULL if unwanted */
bool is_valid_wasm  (const uint8_t *buf, size_t len, module_t *m);

/* On valid item function will increment *buf to next item */
/* module_t m can be NULL if unwanted */
bool is_valid_instr    (const uint8_t **buf, size_t len, module_t *m);
bool is_valid_section  (size_t num, const uint8_t **buf, size_t len, module_t *m);

void print_counts(module_t *m);

#endif
