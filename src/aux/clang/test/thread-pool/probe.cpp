// Observe actual LLVM task queues and the guest's deferred result ownership.
// Functional expectations are in thread-pool.wast.
#include "llvm/Support/ThreadPool.h"
#include <cstdint>
#include <memory>
#include <type_traits>
#include <unistd.h>

struct Token {
  unsigned &Destroyed;
  int Value;
  Token(unsigned &Destroyed, int Value) : Destroyed(Destroyed), Value(Value) {}
  ~Token() { ++Destroyed; }
};

struct alignas(64) AlignedResult {
  unsigned &Live;
  int Value;
  AlignedResult(unsigned &Live, int Value) : Live(Live), Value(Value) { ++Live; }
  AlignedResult(AlignedResult &&Other) : Live(Other.Live), Value(Other.Value) {
    ++Live;
  }
  ~AlignedResult() { --Live; }
};

struct ReferenceResult {
  unsigned Value;
  ReferenceResult *operator&() = delete;
};

extern "C" int main(int argc, char **argv, char **environment) {
  (void)argc; (void)argv; (void)environment;
  unsigned Output[128] = {};
  unsigned Count = 0;
  auto record = [&](unsigned Value) { Output[Count++] = Value; };
  using llvm::ThreadPoolFuture;
  using llvm::SingleThreadExecutor;
  using llvm::ThreadPoolTaskGroup;

  // profile and returned interfaces
  record(LLVM_WASTE_GUEST);
  record(LLVM_ENABLE_THREADS);
  record(_LIBCPP_HAS_THREADS);
  record(ThreadPoolFuture<int>().valid());
  record(std::is_copy_constructible<ThreadPoolFuture<int>>::value);
  record((std::is_same<decltype(std::declval<ThreadPoolFuture<int>>().get()),
                      const int &>::value));
  record((std::is_same<decltype(std::declval<ThreadPoolFuture<void>>().get()),
                      void>::value));
  record((std::is_same<decltype(std::declval<ThreadPoolFuture<int &>>().get()),
                      int &>::value));

  // submission, sharing, selective future waits and FIFO executor draining
  {
    SingleThreadExecutor Pool;
    unsigned Calls = 0, Order = 0;
    record(Pool.getMaxConcurrency());
    auto First = Pool.async([&] { ++Calls; Order = Order * 10 + 1; return 17; });
    auto Second = Pool.async([&] { ++Calls; Order = Order * 10 + 2; return 23; });
    record(Calls);
    record(First.valid());
    auto Copy = First;
    First = {};
    record(First.valid());
    record(Copy.valid());
    Copy.wait();
    record(Calls);
    record(Order);
    record(Copy.get());
    record(&Copy.get() == &Copy.get());
    Pool.wait();
    record(Calls);
    record(Order);
    record(Second.get());
    Pool.wait();
    record(Calls);
    auto Moved = std::move(Copy);
    record(Copy.valid());
    record(Moved.valid());
    record(Moved.get());
  }

  // a future get may execute a later task before the FIFO queue is drained
  {
    SingleThreadExecutor Pool;
    unsigned Order = 0;
    Pool.async([&] { Order = Order * 10 + 1; });
    auto Later = Pool.async([&] { Order = Order * 10 + 2; });
    record(Order);
    Later.get();
    record(Order);
    Pool.wait();
    record(Order);
    Later.wait();
    record(Order);
  }

  // dropping the caller's handle neither runs nor discards the queued task
  {
    SingleThreadExecutor Pool;
    unsigned Calls = 0, Destroyed = 0;
    {
      auto Future = Pool.async(
          [Owned = std::make_unique<Token>(Destroyed, 31), &Calls]() mutable {
            ++Calls;
            return Owned->Value;
          });
      record(Future.valid());
    }
    record(Calls);
    record(Destroyed);
    Pool.wait();
    record(Calls);
    record(Destroyed);
    Pool.wait();
    record(Calls);
    ReferenceResult Result{71};
    auto ObjectReference = Pool.async([&]() -> ReferenceResult & { return Result; });
    record(std::addressof(ObjectReference.get()) == std::addressof(Result));
    ObjectReference.get().Value = 73;
    record(Result.Value);
    Pool.wait();
  }

  // shared move-only results survive until the last result owner releases them
  {
    SingleThreadExecutor Pool;
    unsigned Destroyed = 0;
    auto Future = Pool.async([&] { return std::make_unique<Token>(Destroyed, 42); });
    auto Copy = Future;
    record(Destroyed);
    record(Future.get()->Value);
    record(Future.get().get() == Copy.get().get());
    Pool.wait();
    record(Destroyed);
    Future = {};
    record(Destroyed);
    Copy = {};
    record(Destroyed);
  }

  // non-default-constructible and over-aligned result storage
  {
    SingleThreadExecutor Pool;
    unsigned Live = 0;
    auto Future = Pool.async([&] { return AlignedResult(Live, 83); });
    auto Copy = Future;
    record(Live);
    record(Future.get().Value);
    record(Live);
    record((reinterpret_cast<uintptr_t>(&Future.get()) & 63) == 0);
    record(&Future.get() == &Copy.get());
    Pool.wait();
    Future = {};
    record(Live);
    Copy = {};
    record(Live);
  }

  // reference identity, const references and bound arguments
  {
    SingleThreadExecutor Pool;
    int Value = 19;
    auto Reference = Pool.async([&]() -> int & { return Value; });
    record(&Reference.get() == &Value);
    Reference.get() = 29;
    record(Value);
    auto ConstReference = Pool.async([&]() -> const int & { return Value; });
    ConstReference.wait();
    Value = 33;
    record(ConstReference.get());
    record(&ConstReference.get() == &Value);
    unsigned Calls = 0;
    auto Bound = Pool.async([&](int &Target, int Add) {
      ++Calls; Target += Add; return Target;
    }, std::ref(Value), 9);
    record(Calls);
    record(Bound.get());
    record(Value);
    record(Calls);
    Pool.wait();
    record(Calls);
  }

  // void results share execution and remain valid after get
  {
    SingleThreadExecutor Pool;
    unsigned Calls = 0;
    auto Future = Pool.async([&] { ++Calls; });
    auto Copy = Future;
    record(Calls);
    Future.get();
    Copy.get();
    record(Calls);
    record(Future.valid());
    Pool.wait();
    record(Calls);
  }

  // SingleThreadExecutor group waits intentionally drain all groups, including
  // recursive submission to another group after the running task was popped.
  {
    SingleThreadExecutor Pool;
    ThreadPoolTaskGroup First(Pool), Second(Pool);
    unsigned Order = 0;
    First.async([&] {
      Order = Order * 10 + 1;
      ThreadPoolTaskGroup Nested(Pool);
      Nested.async([&] { Order = Order * 10 + 3; });
      Nested.wait();
      Order = Order * 10 + 4;
    });
    Second.async([&] { Order = Order * 10 + 2; });
    record(Order);
    First.wait();
    record(Order);
    Second.wait();
    record(Order);
  }

  // nested future get forces only its own deferred state; the later queue thunk
  // observes completion and does not execute it again
  {
    SingleThreadExecutor Pool;
    unsigned Order = 0;
    auto Outer = Pool.async([&] {
      Order = Order * 10 + 1;
      auto Inner = Pool.async([&] { Order = Order * 10 + 2; return 7; });
      unsigned Value = Inner.get();
      Order = Order * 10 + 3;
      return Value + 1;
    });
    record(Order);
    Pool.wait();
    record(Order);
    record(Outer.get());
    record(Order);
  }

  // task-group destruction drains queued tasks without caller-held futures
  {
    SingleThreadExecutor Pool;
    unsigned Order = 0;
    Pool.async([&] { Order = Order * 10 + 1; });
    {
      ThreadPoolTaskGroup Group(Pool);
      Group.async([&](unsigned Digit) { Order = Order * 10 + Digit; }, 2U);
      record(Order);
    }
    record(Order);
    Pool.wait();
    record(Order);
  }

  // executor destruction drains tasks; an external shared result survives it
  {
    ThreadPoolFuture<int> Survivor;
    unsigned Calls = 0;
    {
      SingleThreadExecutor Pool;
      Survivor = Pool.async([&] { ++Calls; return 61; });
      Pool.async([&] { ++Calls; });
      record(Calls);
    }
    record(Calls);
    record(Survivor.valid());
    record(Survivor.get());
    Survivor.wait();
    record(Calls);
  }
  return write(1, Output, Count * sizeof(Output[0])) !=
         (ssize_t)(Count * sizeof(Output[0]));
}
