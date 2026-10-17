// Guest-only deferred results for LLVM's SingleThreadExecutor.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#ifndef WASTE_LLVM_SUPPORT_THREADPOOLFUTURE_H
#define WASTE_LLVM_SUPPORT_THREADPOOLFUTURE_H

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/Config/llvm-config.h"
#include <cassert>
#include <memory>
#include <optional>
#include <utility>

#if !defined(LLVM_WASTE_GUEST) || LLVM_ENABLE_THREADS
#error "This deferred result belongs only to the single-thread WASTE guest"
#endif

namespace llvm {
namespace detail {
template <typename T> class ThreadPoolTaskResult {
  std::optional<T> Value;

public:
  using ReturnType = const T &;
  void run(unique_function<T()> &Task) { Value.emplace(Task()); }
  ReturnType get() const { return *Value; }
};

template <typename T> class ThreadPoolTaskResult<T &> {
  T *Value = nullptr;

public:
  using ReturnType = T &;
  void run(unique_function<T &()> &Task) { Value = std::addressof(Task()); }
  ReturnType get() const { return *Value; }
};

template <> class ThreadPoolTaskResult<void> {
public:
  using ReturnType = void;
  void run(unique_function<void()> &Task) { Task(); }
  void get() const {}
};
} // namespace detail

// Submission only owns a callable. A future wait/get or the executor's queued
// thunk runs it once. Copies retain the same result; destroying a handle never
// runs a task. The executor retains its own handle until it drains the queue.
// This is an LLVM-owned interface, not a replacement facility in namespace std.
template <typename T> class ThreadPoolFuture {
  struct State {
    unique_function<T()> Task;
    detail::ThreadPoolTaskResult<T> Result;
    bool Running = false;
    bool Complete = false;

    explicit State(unique_function<T()> Task) : Task(std::move(Task)) {}
    void wait() {
      if (Complete)
        return;
      assert(!Running && "A deferred task cannot wait on itself");
      Running = true;
      Result.run(Task);
      Complete = true;
      Running = false;
      Task = nullptr;
    }
  };
  std::shared_ptr<State> Shared;

  explicit ThreadPoolFuture(unique_function<T()> Task)
      : Shared(std::make_shared<State>(std::move(Task))) {}
  friend class ThreadPoolInterface;

public:
  ThreadPoolFuture() = default;
  bool valid() const noexcept { return bool(Shared); }
  void wait() const {
    assert(Shared && "A deferred result must have a task");
    Shared->wait();
  }
  typename detail::ThreadPoolTaskResult<T>::ReturnType get() const {
    wait();
    return Shared->Result.get();
  }
};
} // namespace llvm
#endif
