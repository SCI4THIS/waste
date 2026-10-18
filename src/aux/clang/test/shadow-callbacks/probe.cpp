// Observe the complete production callback initializer with real LLVM IR.
// The guest WAST owns all expected names, signatures and declaration counts.
#include "AddressSanitizer.cpp"
#include <array>
#include <vector>
#include <unistd.h>

struct CallbackRecord {
  std::array<uint64_t, 6> Fields{};
  std::string Name;
};
struct CallbackCase {
  std::array<uint64_t, 8> Header{};
  std::vector<CallbackRecord> Callbacks;
};

static CallbackCase capture(unsigned Bits,
                            AsanDetectStackUseAfterReturnMode ReturnMode,
                            bool Scope) {
  CallbackCase Result;
  LLVMContext Context;
  Module M("shadow-callbacks", Context);
  M.setTargetTriple(Triple(Bits == 32 ? "wasm32-unknown-unknown"
                                    : "wasm64-unknown-unknown"));
  M.setDataLayout(Bits == 32 ? "e-p:32:32" : "e-p:64:64");
  auto *Intptr = Type::getIntNTy(Context, Bits);
  auto *CallbackType = FunctionType::get(Type::getVoidTy(Context),
                                         {Intptr, Intptr}, false);
  Value *Existing = M.getOrInsertFunction("__asan_set_shadow_f1",
                                           CallbackType).getCallee();
  auto *Owner = Function::Create(
      FunctionType::get(Type::getVoidTy(Context), false),
      GlobalValue::ExternalLinkage, "owner", M);
  Result.Header[0] = Bits;
  Result.Header[1] = M.size();
  AddressSanitizer Pass(M, nullptr, 0, 0, false, false, Scope, ReturnMode);
  RuntimeCallInserter Calls(*Owner);
  FunctionStackPoisoner Stack(*Owner, Pass, Calls);
  Stack.initializeCallbacks(M);
  Result.Header[2] = M.size();
  Stack.initializeCallbacks(M);
  Result.Header[3] = M.size();
  for (auto &Callee : Stack.AsanSetShadowFunc)
    Result.Header[4] += Callee.getCallee() != nullptr;
  Result.Header[5] = Stack.AsanSetShadowFunc[0xf1].getCallee() == Existing;
  Result.Header[6] = (Stack.AsanSetShadowFunc[0x08].getCallee() != nullptr) |
                    ((Stack.AsanSetShadowFunc[0xff].getCallee() != nullptr) << 1);
  for (const Function &F : M)
    Result.Header[7] += F.getName().starts_with("__asan_set_shadow_");
  for (unsigned Value : {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                         0xf1, 0xf2, 0xf3, 0xf5, 0xf8}) {
    CallbackRecord Record;
    auto Callee = Stack.AsanSetShadowFunc[Value];
    if (auto *F = dyn_cast_or_null<Function>(Callee.getCallee())) {
      Record.Name = F->getName().str();
      auto *Type = F->getFunctionType();
      Record.Fields = {Record.Name.size(), Type->getReturnType()->isVoidTy(),
                       Type->getNumParams(),
                       Type->getNumParams() > 0 && Type->getParamType(0)->isIntegerTy()
                           ? Type->getParamType(0)->getIntegerBitWidth() : 0,
                       Type->getNumParams() > 1 && Type->getParamType(1)->isIntegerTy()
                           ? Type->getParamType(1)->getIntegerBitWidth() : 0,
                       Type->isVarArg()};
    }
    Result.Callbacks.push_back(std::move(Record));
  }
  return Result;
}

extern "C" int main(int, char **, char **) {
  std::vector<CallbackCase> Results;
  Results.push_back(capture(32, AsanDetectStackUseAfterReturnMode::Never, false));
  Results.push_back(capture(32, AsanDetectStackUseAfterReturnMode::Runtime, false));
  Results.push_back(capture(64, AsanDetectStackUseAfterReturnMode::Always, true));
  // The captures outlive every module, context, callback owner and stream.
  for (const auto &Case : Results) {
    (void)write(1, Case.Header.data(), sizeof(Case.Header));
    for (const auto &Callback : Case.Callbacks) {
      (void)write(1, Callback.Fields.data(), sizeof(Callback.Fields));
      (void)write(1, Callback.Name.data(), Callback.Name.size());
    }
  }
  return 37;
}
