// Observe the complete production source and its private interval formatter.
// Only this observer uses -fno-access-control; WAST owns all expectations.
#include "AssignmentTrackingAnalysis.cpp"
#include "llvm/IR/LLVMContext.h"
#include <unistd.h>

static void emit(uint64_t Value) { (void)write(1, &Value, sizeof(Value)); }

extern "C" int main(int, char **, char **) {
  std::vector<std::string> Output;
  {
    LLVMContext Context;
    Module M("assignment-format", Context);
    auto *FType = FunctionType::get(Type::getVoidTy(Context),
        {PointerType::getUnqual(Context), PointerType::getUnqual(Context)}, false);
    auto *F = Function::Create(FType, GlobalValue::ExternalLinkage, "owner", M);
    MemLocFragmentFill Owner(*F, nullptr, false);
    using Map = MemLocFragmentFill::FragsInMemMap;
    Map Empty(Owner.IntervalMapAlloc);
    Output.push_back(Owner.toString(Empty.begin()));
    Output.push_back(Owner.toString(Empty.begin(), false));

    Map None(Owner.IntervalMapAlloc);
    None.insert(0, 8, 0);
    Output.push_back(Owner.toString(None.begin()));
    Output.push_back(Owner.toString(None.begin(), false));

    F->getArg(0)->setName("edge");
    const unsigned EdgeID = Owner.Bases.insert(
        RawLocationWrapper(ValueAsMetadata::get(F->getArg(0))));
    Map Edge(Owner.IntervalMapAlloc);
    Edge.insert(4294967294U, 4294967295U, EdgeID);
    Output.push_back(Owner.toString(Edge.begin(), false));

    F->getArg(1)->setName(std::string(160, 'L'));
    const unsigned LongID = Owner.Bases.insert(
        RawLocationWrapper(ValueAsMetadata::get(F->getArg(1))));
    Map Long(Owner.IntervalMapAlloc);
    Long.insert(1000000000U, 2000000000U, LongID);
    Output.push_back(Owner.toString(Long.begin()));

    const char BinaryName[] = {'n', '\0', 'x', '\n', '\x7f'};
    F->getArg(1)->setName(StringRef(BinaryName, sizeof(BinaryName)));
    Map Binary(Owner.IntervalMapAlloc);
    Binary.insert(16, 24, LongID);
    Output.push_back(Owner.toString(Binary.begin()));

    F->getArg(1)->setName("");
    Map Unnamed(Owner.IntervalMapAlloc);
    Unnamed.insert(32, 64, LongID);
    Output.push_back(Owner.toString(Unnamed.begin()));

    F->getArg(0)->setName("renamed");
    Output.push_back(Owner.toString(Edge.begin()));
    auto End = Edge.begin();
    ++End;
    Output.push_back(Owner.toString(End));
  }
  // Captures outlive every IR, interval-map and formatter owner above.
  emit(Output.size());
  for (const auto &Text : Output) emit(Text.size());
  for (const auto &Text : Output) (void)write(1, Text.data(), Text.size());
  return 36;
}
