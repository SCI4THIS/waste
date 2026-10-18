// Observe production ExtTSP APIs and real option parsing; WAST owns assertions.
#include "llvm/Transforms/Utils/CodeLayout.h"
#include "llvm/Support/CommandLine.h"
#include <unistd.h>

using namespace llvm;
using namespace llvm::codelayout;

static void emit(uint64_t Value) { (void)write(1, &Value, sizeof(Value)); }
static void emitScore(double Value) {
  union { double Value; uint64_t Bits; } Data = {Value};
  emit(Data.Bits);
}

struct Observation {
  std::vector<uint64_t> Order;
  double Original, Optimized, Explicit;
};

static Observation observe(std::vector<uint64_t> Sizes,
                           std::vector<uint64_t> Counts,
                           std::vector<EdgeCount> Edges,
                           std::vector<uint64_t> ExpectedOrder) {
  auto Order = computeExtTspLayout(Sizes, Counts, Edges);
  double Original = calcExtTspScore(Sizes, Edges);
  double Optimized = calcExtTspScore(Order, Sizes, Edges);
  double Explicit = calcExtTspScore(ExpectedOrder, Sizes, Edges);
  // Order remains live after all input vectors are destroyed.
  return {std::move(Order), Original, Optimized, Explicit};
}

extern "C" int main(int Argc, char **Argv, char **) {
  cl::ParseCommandLineOptions(Argc, Argv);
  const auto &Options = cl::getRegisteredOptions();
  for (const char *Name : {"ext-tsp-forward-distance", "ext-tsp-max-chain-size",
                           "cdsort-cache-entries", "cdsort-distance-power"})
    emit(Options.find(Name) != Options.end());

  std::vector<Observation> Results;
  Results.push_back(observe({10, 10, 10}, {100, 100, 100},
      {{0, 2, 100}, {2, 1, 100}}, {0, 2, 1}));
  Results.push_back(observe({16, 16, 16, 16}, {100, 100, 100, 100},
      {{0, 1, 100}, {1, 2, 100}, {2, 3, 100}}, {0, 1, 2, 3}));
  Results.push_back(observe({10, 20, 40}, {0, 0, 0}, {}, {0, 1, 2}));
  Results.push_back(observe({10, 10, 10, 10}, {100, 0, 100, 100},
      {{0, 2, 100}, {2, 3, 100}}, {0, 2, 3, 1}));
  Results.push_back(observe({16, 32, 8, 16}, {100, 90, 10, 100},
      {{0, 1, 90}, {0, 2, 10}, {1, 3, 90}, {2, 3, 10}}, {0, 1, 3, 2}));
  Results.push_back(observe({10, 10, 10}, {100, 100, 100},
      {{0, 1, 100}, {1, 2, 100}, {2, 1, 90}}, {0, 1, 2}));
  Results.push_back(observe({10, 10, 10}, {100, 100, 0},
      {{0, 1, 100}, {0, 2, 0}}, {0, 1, 2}));

  for (const auto &Result : Results) {
    emit(Result.Order.size());
    for (auto Node : Result.Order) emit(Node);
    emitScore(Result.Original);
    emitScore(Result.Optimized);
    emitScore(Result.Explicit);
  }
  emitScore(calcExtTspScore({10, 1024, 10}, {{0, 2, 100}}));
  emitScore(calcExtTspScore({10, 1025, 10}, {{0, 2, 100}}));
  emitScore(calcExtTspScore({10, 1023, 10}, {{0, 2, 100}}));
  emitScore(calcExtTspScore({1, 0}, {10, 630}, {{0, 1, 100}}));
  emitScore(calcExtTspScore({1, 0}, {10, 631}, {{0, 1, 100}}));
  emitScore(calcExtTspScore({1, 0}, {10, 629}, {{0, 1, 100}}));
  emitScore(calcExtTspScore({10, 10, 10}, {{0, 1, 0}}));
  emitScore(calcExtTspScore({10, 10, 10}, {{0, 0, 100}}));
  return 35;
}
