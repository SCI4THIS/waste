// Observe the production CFG node formatter; expectations belong in WAST.
#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <fcntl.h>
#include <unistd.h>
#include <vector>

using namespace llvm;

static void emit(uint64_t Value) { (void)write(1, &Value, sizeof(Value)); }

extern "C" int main(int, char **, char **) {
  LLVMContext Context;
  Module M("cfg-format", Context);
  auto *F = Function::Create(FunctionType::get(Type::getVoidTy(Context), false),
                             GlobalValue::ExternalLinkage, "layout", M);
  auto *Entry = BasicBlock::Create(Context, "entry", F);
  ReturnInst::Create(Context, nullptr, Entry);
  BlockFrequencyInfo Frequencies;
  DOTGraphTraits<DOTFuncInfo *> Traits(false);
  std::vector<std::string> Output;
  unsigned TotalCallbacks = 0;
  int SleepStatus = -123;

  for (unsigned Case = 0; Case != 11; ++Case) {
    const bool HasFormatter = Case != 0 && Case != 5;
    const bool ReturnsId = Case != 1 && Case != 7;
    const bool Heat = Case >= 5;
    const bool Yield = Case == 10;
    std::string Id = "owned-entry";
    if (Case == 3 || Case == 8)
      Id.clear();
    if (Case == 4)
      Id = std::string("A\0B", 3);
    if (Case == 9)
      Id.assign(256, 'q');
    if (Yield)
      Id = "yield-entry";
    unsigned Callbacks = 0;
    bool SawEntry = false;
    std::optional<std::function<std::optional<std::string>(const BasicBlock *)>>
        Formatter;
    if (HasFormatter)
      Formatter = [Id, ReturnsId, Yield, Entry, &Callbacks, &SawEntry,
                   &SleepStatus](const BasicBlock *Node)
          -> std::optional<std::string> {
        ++Callbacks;
        SawEntry = Node == Entry;
        if (Yield)
          SleepStatus = usleep(2000);
        if (!ReturnsId)
          return std::nullopt;
        return Id;
      };
    DOTFuncInfo Info(F, &Frequencies, nullptr, 16, Formatter);
    Info.setHeatColors(Heat);
    Id.assign("mutated-source");
    Formatter.reset();
    auto Attributes = Traits.getNodeAttributes(Entry, &Info);
    emit(Attributes.size());
    emit(Info.getFreq(Entry));
    emit(Info.getNodeIdFormatter().has_value());
    emit(Callbacks);
    emit(Info.showHeatColors());
    emit(SawEntry);
    TotalCallbacks += Callbacks;
    Output.push_back(std::move(Attributes));
  }

  const int FD = open("/tmp/cfg-attributes.txt", O_WRONLY | O_CREAT | O_TRUNC,
                      0644);
  uint64_t Bytes = 0;
  for (const auto &Attributes : Output) {
    Bytes += Attributes.size() + 1;
    if (FD >= 0) {
      (void)write(FD, Attributes.data(), Attributes.size());
      (void)write(FD, "\n", 1);
    }
  }
  emit(static_cast<int64_t>(SleepStatus));
  emit(TotalCallbacks);
  emit(FD >= 0);
  emit(Bytes);
  if (FD >= 0)
    (void)close(FD);
  return 32;
}
