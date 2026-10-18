// Observe the complete actual source and real IR graph; WAST owns assertions.
#include "ImportedFunctionsInliningStatistics.cpp"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

using namespace llvm;

static void emit(uint64_t Value) { (void)write(1, &Value, sizeof(Value)); }

extern "C" int main(int, char **, char **) {
  std::vector<std::string> Output;
  Output.push_back(getStatString("fraction", 1, 3, "units"));
  Output.push_back(getStatString("zero", 1, 0, "units"));
  Output.push_back(getStatString("full", 7, 7, "units", false));
  Output.push_back(getStatString("small", 1, 10000000, "units"));
  Output.push_back(getStatString("large", 2147483647, 1, "units"));
  Output.push_back(getStatString("negative", -1, 3, "units"));
  Output.push_back(getStatString("empty", 0, 3, "units"));
  std::string Label = "owned-copy";
  Output.push_back(getStatString(Label.c_str(), 2, 3, "units"));
  Label.assign("mutated-source");
  char Compiled[96];
  const int CompiledSize = snprintf(Compiled, sizeof(Compiled),
      "compiled: %*.*g|%d|%.3Lf|%s", 10, 4, 100.0 / 3.0, 17, 1.125L, "tail");
  emit(static_cast<int64_t>(CompiledSize));
  if (CompiledSize >= 0 && CompiledSize < static_cast<int>(sizeof(Compiled)))
    Output.emplace_back(Compiled, CompiledSize);

  const int FD = open("/tmp/inlining-text.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  uint64_t Total = 0;
  for (const auto &Text : Output) {
    emit(Text.size());
    Total += Text.size();
    if (FD >= 0) (void)write(FD, Text.data(), Text.size());
  }
  emit(Total);
  emit(FD >= 0);
  if (FD >= 0) (void)close(FD);

  LLVMContext Context;
  Module M("inlining-module", Context);
  auto Create = [&](const char *Name, bool Imported) {
    auto *F = Function::Create(FunctionType::get(Type::getVoidTy(Context), false),
                               GlobalValue::ExternalLinkage, Name, M);
    auto *Entry = BasicBlock::Create(Context, "entry", F);
    ReturnInst::Create(Context, nullptr, Entry);
    if (Imported)
      F->setMetadata("thinlto_src_module", MDNode::get(Context,
          MDString::get(Context, "upstream-module")));
    return F;
  };
  auto *Entry = Create("entry", false);
  auto *Imported = Create("imported", true);
  auto *Helper = Create("helper", false);
  (void)Create("unused", true);
  ImportedFunctionsInliningStatistics Stats;
  Stats.setModuleInfo(M);
  Stats.recordInline(*Entry, *Imported);
  Stats.recordInline(*Imported, *Helper);
  Entry->setName("mutated-entry");
  Imported->setName("mutated-imported");
  Helper->setName("mutated-helper");
  Stats.dump(true);
  dbgs().flush();
  return 33;
}
