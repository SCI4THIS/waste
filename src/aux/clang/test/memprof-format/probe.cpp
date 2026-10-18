// Observe the complete production source; only this fixture opens private access.
#include "MemProfContextDisambiguation.cpp"
#include <array>
#include <cstdint>
#include <unistd.h>

using IRGraph = CallsiteContextGraph<ModuleCallsiteContextGraph, Function,
                                     Instruction *>;
using SummaryGraph = CallsiteContextGraph<IndexCallsiteContextGraph,
                                          FunctionSummary, IndexCall>;

template <typename Graph>
static void captureBoundaries(std::vector<std::string> &Output) {
  using Traits = DOTGraphTraits<const Graph *>;
  for (uintptr_t Value : std::array<uintptr_t, 10>{
           0, 1, 15, 16, 255, 256, 0xabcdef, 0x7fffffff, 0x80000000U,
           0xffffffffU})
    // These numeric pointers are never dereferenced by the ID formatter.
    Output.push_back(Traits::getNodeId(
        reinterpret_cast<typename Traits::NodeRef>(Value)));
}

template <typename Graph>
static void captureNodes(std::vector<std::string> &Output,
                         std::vector<uint64_t> &Addresses) {
  using Node = typename Graph::ContextNode;
  using Edge = typename Graph::ContextEdge;
  using Traits = DOTGraphTraits<const Graph *>;
  Graph Owner;
  Traits Formatter;
  Node Original(true), Caller(false), Clone(true);
  Original.AllocTypes = static_cast<uint8_t>(AllocationType::Cold);
  Caller.AllocTypes = static_cast<uint8_t>(AllocationType::NotCold);
  Clone.AllocTypes = Original.AllocTypes | Caller.AllocTypes;
  auto Incoming = std::make_shared<Edge>(
      &Original, &Caller, Original.AllocTypes, DenseSet<uint32_t>{9, 1, 4});
  Original.CallerEdges.push_back(Incoming);
  Caller.CalleeEdges.push_back(Incoming);
  Original.addClone(&Clone);
  for (Node *N : {&Original, &Clone}) {
    Addresses.push_back(reinterpret_cast<uintptr_t>(N));
    Output.push_back(Formatter.getNodeId(N));
    Output.push_back(Formatter.getNodeAttributes(N, &Owner));
  }
}

static void emit(uint64_t Value) { (void)write(1, &Value, sizeof(Value)); }

extern "C" int main(int, char **, char **) {
  std::vector<std::string> Output;
  std::vector<uint64_t> Addresses;
  captureBoundaries<IRGraph>(Output);
  captureBoundaries<SummaryGraph>(Output);
  captureNodes<IRGraph>(Output, Addresses);
  captureNodes<SummaryGraph>(Output, Addresses);
  // Every real graph, node, edge and formatter has been destroyed.
  emit(Output.size());
  for (auto Address : Addresses) emit(Address);
  for (const auto &Text : Output) emit(Text.size());
  for (const auto &Text : Output) (void)write(1, Text.data(), Text.size());
  return 38;
}
