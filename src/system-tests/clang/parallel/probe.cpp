// Observe real LLVM latch, task groups and scalar algorithms; WAST asserts.
#include "llvm/Support/Parallel.h"
#include <cstdint>
#include <memory>
#include <numeric>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>
#include <vector>

static int32_t Output[256];
static unsigned Count;
static void record(int32_t Value) { Output[Count++] = Value; }
struct Token {
  unsigned &Live, &Destroyed;
  Token(unsigned &Live, unsigned &Destroyed) : Live(Live), Destroyed(Destroyed) {
    ++Live;
  }
  ~Token() { --Live; ++Destroyed; }
};
using Latch = llvm::parallel::detail::Latch;
using TaskGroup = llvm::parallel::TaskGroup;

static int observe_rejection(unsigned Mode) {
  int Child = fork();
  if (Child == 0) {
    {
      Latch L(Mode == 0 || Mode == 2 ? 1 : Mode == 3 ? UINT32_MAX : 0);
      if (Mode == 0) L.sync();
      else if (Mode == 1) L.dec();
      else if (Mode == 3) L.inc();
    }
    _exit(91);
  }
  if (Child < 0) return 1;
  record(Child > 0);
  int Status = -1;
  record(waitpid(Child, &Status, 0) == Child);
  record(Status);
  record(WIFSIGNALED(Status));
  record(WTERMSIG(Status));
  return 0;
}

extern "C" int main(int argc, char **argv, char **environment) {
  (void)argc; (void)argv; (void)environment;
  record(LLVM_WASTE_GUEST);
  record(LLVM_ENABLE_THREADS);
  record(_LIBCPP_HAS_THREADS);
#ifdef NDEBUG
  record(1);
#else
  record(0);
#endif
  record(std::is_copy_constructible<Latch>::value);
  record(std::is_move_constructible<Latch>::value);
  record(llvm::parallel::getThreadCount());
  record(sizeof(Latch));

  // Atomic count ownership, repeated zero sync and nonzero initial counts.
  {
    Latch L;
    record(L.getCount());
    L.sync(); L.sync(); record(L.getCount());
    L.inc(); record(L.getCount());
    L.inc(); record(L.getCount());
    L.dec(); record(L.getCount());
    L.dec(); record(L.getCount());
    const auto &Const = L;
    Const.sync(); record(Const.getCount());
  }
  {
    Latch L(3);
    record(L.getCount());
    L.inc(); record(L.getCount());
    for (unsigned I = 0; I != 4; ++I) { L.dec(); record(L.getCount()); }
    L.sync(); record(L.getCount());
  }

  // Spawn is synchronous, including recursion through an enclosing group.
  unsigned Calls = 0, Order = 0;
  {
    TaskGroup G;
    record(G.isParallel()); record(Calls);
    G.spawn([&] { ++Calls; Order = Order * 10 + 1; });
    record(Calls); record(Order);
    G.spawn([&] {
      ++Calls; Order = Order * 10 + 2;
      TaskGroup Nested;
      record(Nested.isParallel());
      Nested.spawn([&] {
        ++Calls; Order = Order * 10 + 3;
        G.spawn([&] { ++Calls; Order = Order * 10 + 4; });
      });
    });
    record(Calls); record(Order);
  }
  record(Calls); record(Order);
  unsigned Live = 0, Destroyed = 0;
  {
    TaskGroup G;
    auto Owned = std::make_shared<Token>(Live, Destroyed);
    G.spawn([Owned, &Live] { record(Live); });
    record(Destroyed);
    Owned.reset(); record(Live); record(Destroyed);
    std::string Source(128, 'x');
    std::function<void()> F = [Source, &Calls, &Order] {
      record(Source.size()); record(Source[0]);
      ++Calls; Order = Order * 10 + 5;
    };
    Source.clear();
    G.spawn(std::move(F));
    record(Calls); record(Order);
  }
  record(Live); record(Destroyed); record(Calls);

  // The actual out-of-line loop visits only the requested range.
  unsigned Seen[8] = {99,99,99,99,99,99,99,99}, LoopCalls = 0, LoopOrder = 0;
  llvm::parallelFor(2, 6, [&](size_t I) {
    ++LoopCalls; LoopOrder = LoopOrder * 10 + I; Seen[I] = I + 10;
  });
  record(LoopCalls); record(LoopOrder);
  for (auto Value : Seen) record(Value);
  llvm::parallelFor(4, 4, [&](size_t) { ++LoopCalls; });
  record(LoopCalls);
  unsigned NestedCalls = 0, NestedSum = 0;
  llvm::parallelFor(0, 3, [&](size_t I) {
    llvm::parallelFor(0, 3, [&](size_t J) { ++NestedCalls; NestedSum += I * 3 + J; });
  });
  record(NestedCalls); record(NestedSum);
  int Range[] = {2,3,5,7};
  unsigned EachCalls = 0;
  llvm::parallelForEach(Range, [&](int &Value) { ++EachCalls; Value *= 2; });
  record(EachCalls);
  for (auto Value : Range) record(Value);

  // Empty/nonempty reductions, move-only accumulation and owning strings.
  int Values[] = {1,2,3,4};
  unsigned Transforms = 0, Reductions = 0, TransformOrder = 0;
  auto Transform = [&](int Value) {
    ++Transforms; TransformOrder = TransformOrder * 10 + Value; return Value * 3;
  };
  auto Reduce = [&](int Result, int Value) { ++Reductions; return Result + Value; };
  record(llvm::parallelTransformReduce(Values, Values, 17, Reduce, Transform));
  record(Transforms); record(Reductions);
  record(llvm::parallelTransformReduce(Values, 10, Reduce, Transform));
  record(Transforms); record(Reductions); record(TransformOrder);
  auto Initial = std::make_unique<unsigned>(10);
  auto *Identity = Initial.get();
  auto Moved = llvm::parallelTransformReduce(
      std::begin(Values), std::end(Values), std::move(Initial),
      [](std::unique_ptr<unsigned> Result, int Value) { *Result += Value; return Result; },
      [](int Value) { return Value * Value; });
  record(Moved.get() == Identity); record(*Moved);
  const char *Strings[] = {"a", "bb", "ccc"};
  auto Text = llvm::parallelTransformReduce(
      Strings, std::string("!"),
      [](std::string Result, const char *Value) { Result += Value; return Result; },
      [](const char *Value) { return Value; });
  record(Text.size());
  for (char Value : Text) record(Value);

  // Range fences, duplicates, negative values and custom descending order.
  int Sorted[] = {111,7,-3,7,0,-9,2,1,2,222};
  llvm::parallelSort(Sorted + 1, Sorted + 9);
  for (int Value : Sorted) record(Value);
  llvm::parallelSort(Sorted + 1, Sorted + 9, std::greater<int>());
  for (unsigned I = 1; I != 9; ++I) record(Sorted[I]);
  int Empty = 4;
  unsigned Comparisons = 0;
  llvm::parallelSort(&Empty, &Empty, [&](int A, int B) { ++Comparisons; return A < B; });
  record(Empty); record(Comparisons);
  std::vector<int> Large(4097);
  for (unsigned I = 0; I != Large.size(); ++I) Large[I] = (4097 - I) % 31;
  llvm::parallelSort(Large, std::less<int>());
  record(std::is_sorted(Large.begin(), Large.end()));
  record(Large.front()); record(Large.back());
  record(std::accumulate(Large.begin(), Large.end(), 0));

  // Release builds must reject unfinished waits/destruction and count wrap.
  for (unsigned Mode = 0; Mode != 4; ++Mode)
    if (observe_rejection(Mode)) return 2;
  record(kill(getpid(), 0));
  return write(1, Output, Count * sizeof(Output[0])) < 0;
}
