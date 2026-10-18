// Compile the public ABI as C++, independently of private LLVM headers.
#include <sys/wait.h>
#include <waste/process.h>
static_assert(__is_same(decltype(&waste_waitpid_until),
                       pid_t (*)(pid_t, int *, int, uint64_t)), "wait ABI");
extern "C" pid_t waste_waitpid_until(pid_t, int *, int, uint64_t);
