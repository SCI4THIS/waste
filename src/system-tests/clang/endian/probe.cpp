// Observe the actual LLVM bare-Wasm header path; WAST owns the assertions.
#include "llvm/ADT/bit.h"
#include <climits>
#include <unistd.h>

struct record {
  uint64_t input, llvm_swap, sdk_big, sdk_little;
};

struct report {
  uint32_t metadata[8];
  record values[3];
};

extern "C" int main(int argc, char **argv, char **environment) {
  (void)argc; (void)argv; (void)environment;
  uint32_t wasm_target = 0, host_platform = 0;
#ifdef __wasm32__
  wasm_target = 1;
#endif
#ifdef __linux__
  host_platform |= 1;
#endif
#ifdef __EMSCRIPTEN__
  host_platform |= 2;
#endif
#ifdef _WIN32
  host_platform |= 4;
#endif
#ifdef __unix__
  host_platform |= 8;
#endif
  volatile uint16_t input16 = 0x1234;
  volatile uint32_t input32 = 0x12345678;
  volatile uint64_t input64 = UINT64_C(0x0123456789abcdef);
  report output = {
    {__BYTE_ORDER, BYTE_ORDER, LITTLE_ENDIAN, BIG_ENDIAN,
     llvm::endianness::native == llvm::endianness::little,
     wasm_target, host_platform, CHAR_BIT},
    {
      {input16, llvm::byteswap(uint16_t(input16)), htobe16(input16), htole16(input16)},
      {input32, llvm::byteswap(uint32_t(input32)), htobe32(input32), htole32(input32)},
      {input64, llvm::byteswap(uint64_t(input64)), htobe64(input64), htole64(input64)}
    }
  };
  return write(1, &output, sizeof(output)) != (ssize_t)sizeof(output);
}
