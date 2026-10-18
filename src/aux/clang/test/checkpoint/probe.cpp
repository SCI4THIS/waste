#include <csetjmp>
#include <csignal>
#include <type_traits>
#define PROBE_JUMP std::longjmp
#define PROBE_ALIGN alignof(std::jmp_buf)
#define PROBE_CXX 1
static_assert(std::is_same<std::jmp_buf, jmp_buf>::value);
#include "probe.h"
