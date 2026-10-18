// Execute LLVM's actual fixed-point IR builder and C++ guest ioctl declaration.
#include "llvm/Config/llvm-config.h"
#ifdef WASTE_TEST_NATIVE_THREADS
#undef LLVM_ENABLE_THREADS
#define LLVM_ENABLE_THREADS 1
#endif
#ifdef WASTE_TEST_GUEST_THREADS
#undef LLVM_ENABLE_THREADS
#define LLVM_ENABLE_THREADS 1
#undef LLVM_WASTE_GUEST
#define LLVM_WASTE_GUEST 1
#endif
#include "llvm/IR/FixedPointBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace llvm;
static void emit(uint64_t Value) { (void)write(1, &Value, sizeof(Value)); }
static void observe(Value *V) {
  if (auto *FP = dyn_cast<ConstantFP>(V))
    emit(FP->getValueAPF().bitcastToAPInt().getZExtValue());
  else if (auto *Integer = dyn_cast<ConstantInt>(V))
    emit(Integer->getSExtValue());
  else
    emit(UINT64_MAX);
}

#if defined(__wasm__)
extern "C"
#endif
int main(int, char **) {
  LLVMContext Context;
  IRBuilder<> Builder(Context);
  FixedPointBuilder<IRBuilder<>> Fixed(Builder);
  // Volatile scales keep the provider call observable in the compiled fixture.
  volatile unsigned Scales[] = {0, 8, 31, 40};
  for (unsigned I = 0; I != 4; ++I) {
    unsigned Scale = Scales[I];
    FixedPointSemantics Sema(64, Scale, true, false, false);
    observe(Fixed.CreateFixedToFloating(
        Builder.getInt64(INT64_C(3) << Scale), Sema, Builder.getDoubleTy()));
    observe(Fixed.CreateFloatingToFixed(
        ConstantFP::get(Builder.getDoubleTy(), -1.5), Sema));
  }
  struct winsize Size = {};
  errno = 0;
  emit((int64_t)ioctl(-1, TIOCGWINSZ, &Size));
  emit(errno);
  return 37;
}
