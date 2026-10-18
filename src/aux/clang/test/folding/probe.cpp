// Observe production constant folding; WAST owns every expected outcome.
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/TargetParser/Triple.h"
#include <errno.h>
#include <unistd.h>

using namespace llvm;

static void emit(uint64_t Value) { (void)write(1, &Value, sizeof(Value)); }

static void observe(Constant *Value) {
  uint64_t Kind = 0, Bits = 0;
  if (auto *FP = dyn_cast_or_null<ConstantFP>(Value)) {
    Kind = 1;
    Bits = FP->getValueAPF().bitcastToAPInt().getZExtValue();
  } else if (auto *Integer = dyn_cast_or_null<ConstantInt>(Value)) {
    Kind = 2;
    Bits = Integer->getZExtValue();
  } else if (isa_and_nonnull<PoisonValue>(Value)) {
    Kind = 3;
  } else if (Value) {
    Kind = 4;
  }
  emit(Kind);
  emit(Bits);
}

extern "C" int main(int, char **, char **) {
  LLVMContext Context;
  Module M("optional-host-folding", Context);
  const DataLayout DL("e-p:32:32-i64:64-n32:64-S128");
  M.setDataLayout(DL);
  M.setTargetTriple(Triple("wasm32-unknown-unknown"));
  auto *Double = Type::getDoubleTy(Context);
  auto *Float = Type::getFloatTy(Context);
  auto *Half = Type::getHalfTy(Context);
  auto *Owner = Function::Create(FunctionType::get(Type::getVoidTy(Context), false),
                                  GlobalValue::ExternalLinkage, "owner", M);
  auto *Entry = BasicBlock::Create(Context, "entry", Owner);
  auto FPConstant = [&](Type *Ty, double Number) { return ConstantFP::get(Ty, Number); };
  auto Unary = [&](Intrinsic::ID ID, Type *Ty, double Number) {
    observe(ConstantFoldIntrinsic(ID, {FPConstant(Ty, Number)}, Ty, DL, Owner));
  };
  errno = 17;
  Unary(Intrinsic::exp, Double, 1.0);
  Unary(Intrinsic::exp2, Double, 3.0);
  Unary(Intrinsic::exp10, Double, 2.0);
  Unary(Intrinsic::log, Double, 2.0);
  Unary(Intrinsic::log10, Double, 100.0);
  Unary(Intrinsic::sin, Double, 0.25);
  Unary(Intrinsic::cos, Double, 0.25);
  Unary(Intrinsic::sinh, Double, 0.25);
  Unary(Intrinsic::cosh, Double, 0.25);
  Unary(Intrinsic::atan, Double, 0.25);
  Unary(Intrinsic::exp, Float, 1.0);
  Unary(Intrinsic::log, Half, 2.0);
  observe(ConstantFoldIntrinsic(Intrinsic::pow,
      {FPConstant(Double, 2.0), FPConstant(Double, 3.0)}, Double, DL, Owner));
  observe(ConstantFoldIntrinsic(Intrinsic::pow,
      {FPConstant(Float, 2.0), FPConstant(Float, 3.0)}, Float, DL, Owner));
  Unary(Intrinsic::log, Double, 1.0);
  Unary(Intrinsic::log, Double, 0.0);
  Unary(Intrinsic::log, Double, -1.0);
  Unary(Intrinsic::log10, Double, 1.0);
  Unary(Intrinsic::log10, Double, 0.0);
  Unary(Intrinsic::log10, Double, -1.0);
  Unary(Intrinsic::atan, Double, -0.0);
  Unary(Intrinsic::log2, Double, 8.0);
  Unary(Intrinsic::sqrt, Double, 9.0);
  Unary(Intrinsic::fabs, Double, -2.5);
  Unary(Intrinsic::floor, Double, -1.25);
  observe(ConstantFoldIntrinsic(Intrinsic::powi,
      {FPConstant(Double, 2.0), ConstantInt::get(Type::getInt32Ty(Context), 3)},
      Double, DL, Owner));
  observe(ConstantFoldIntrinsic(Intrinsic::exp, {PoisonValue::get(Double)},
                                 Double, DL, Owner));
  auto *Vector = FixedVectorType::get(Double, 2);
  auto *Partial = ConstantVector::get({FPConstant(Double, 1.0), FPConstant(Double, 2.0)});
  observe(ConstantFoldIntrinsic(Intrinsic::log, {Partial}, Vector, DL, Owner));
  auto *Exact = ConstantVector::get({FPConstant(Double, 1.0), FPConstant(Double, 0.0)});
  Constant *Folded = ConstantFoldIntrinsic(Intrinsic::log, {Exact}, Vector, DL, Owner);
  observe(Folded);
  observe(Folded ? Folded->getAggregateElement(0U) : nullptr);
  observe(Folded ? Folded->getAggregateElement(1U) : nullptr);

  Unary(Intrinsic::amdgcn_sin, Float, 0.125);
  Unary(Intrinsic::amdgcn_cos, Float, 0.125);
  Unary(Intrinsic::amdgcn_sin, Float, 0.25);
  Unary(Intrinsic::amdgcn_cos, Float, 0.25);
  Unary(Intrinsic::amdgcn_sin, Float, -0.25);
  Unary(Intrinsic::amdgcn_cos, Float, 0.0);
  Unary(Intrinsic::amdgcn_sin, Float, 257.0);

  TargetLibraryInfoImpl Available(Triple("wasm32-unknown-unknown"));
  TargetLibraryInfo TLI(Available);
  auto Call = [&](StringRef Name, Type *Ty, ArrayRef<Constant *> Arguments,
                  const TargetLibraryInfo *Library, bool Allow = true,
                  bool NoBuiltin = false) {
    SmallVector<Type *> Types;
    SmallVector<Value *> Values;
    for (auto *Argument : Arguments) {
      Types.push_back(Argument->getType());
      Values.push_back(Argument);
    }
    auto *F = M.getFunction(Name);
    if (!F)
      F = Function::Create(FunctionType::get(Ty, Types, false),
                             GlobalValue::ExternalLinkage, Name, M);
    auto *CI = CallInst::Create(F->getFunctionType(), F, Values, "", Entry);
    if (NoBuiltin) CI->addFnAttr(Attribute::NoBuiltin);
    observe(ConstantFoldCall(CI, F, Arguments, Library, Allow));
    return CI;
  };
  errno = 17;
  Call("exp", Double, {FPConstant(Double, 1.0)}, &TLI);
  Call("exp2f", Float, {FPConstant(Float, 3.0)}, &TLI);
  Call("log", Double, {FPConstant(Double, 2.0)}, &TLI);
  auto *Pow = Call("pow", Double, {FPConstant(Double, 2.0), FPConstant(Double, 3.0)}, &TLI);
  emit(isMathLibCallNoop(Pow, &TLI));
  Call("acos", Double, {FPConstant(Double, 0.5)}, &TLI);
  Call("asin", Double, {FPConstant(Double, 0.5)}, &TLI);
  Call("atan2", Double, {FPConstant(Double, 1.0), FPConstant(Double, 2.0)}, &TLI);
  Call("logb", Double, {FPConstant(Double, 8.0)}, &TLI);
  Call("log1p", Double, {FPConstant(Double, 0.5)}, &TLI);
  Call("erf", Double, {FPConstant(Double, 0.5)}, &TLI);
  Call("tan", Double, {FPConstant(Double, 0.5)}, &TLI);
  Call("tanh", Double, {FPConstant(Double, 0.5)}, &TLI);
  emit(errno);
  Call("sqrt", Double, {FPConstant(Double, 9.0)}, &TLI);
  Call("sqrt", Double, {FPConstant(Double, 9.0)}, nullptr);
  Call("sqrt", Double, {FPConstant(Double, 9.0)}, &TLI, false);
  Call("sqrt", Double, {FPConstant(Double, 9.0)}, &TLI, true, true);
  TargetLibraryInfoImpl Missing(Triple("wasm32-unknown-unknown"));
  Missing.setUnavailable(LibFunc_sqrt);
  TargetLibraryInfo NoSqrt(Missing);
  Call("sqrt", Double, {FPConstant(Double, 9.0)}, &NoSqrt);
  Call("fmod", Double, {FPConstant(Double, 5.0), FPConstant(Double, 2.0)}, &TLI);
  emit(Entry->size());
  observe(cast<Constant>(Pow->getArgOperand(0)));
  observe(cast<Constant>(Pow->getArgOperand(1)));
  return 34;
}
