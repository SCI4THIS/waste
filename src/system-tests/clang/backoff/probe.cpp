// Observe the actual LLVM implementation; all expectations belong to WAST.
#include "llvm/Support/ExponentialBackoff.h"
#include <signal.h>
#include <unistd.h>

using namespace std::chrono;
static uint64_t Output[64];
static unsigned Count;
#ifdef BACKOFF_CONTROLLED
static volatile sig_atomic_t Signals;
static void handler(int) { ++Signals; }
#endif
static void record(uint64_t Value) { Output[Count++] = Value; }
static_assert(!std::is_copy_constructible<llvm::ExponentialBackoff>::value);

extern "C" int main(int argc, char **argv, char **environment) {
  (void)argc; (void)argv; (void)environment;
  record(LLVM_WASTE_GUEST);
  record(LLVM_ENABLE_THREADS);
  record(_LIBCPP_HAS_THREADS);
  record(llvm::detail::WasteBackoffRandom::min());
  record(llvm::detail::WasteBackoffRandom::max());
  llvm::detail::WasteBackoffRandom Random;
  std::uniform_int_distribution<uint64_t> Distribution(17, 137);
  for (unsigned I = 0; I < 32; ++I)
    record(Distribution(Random));
  llvm::ExponentialBackoff Zero(0ns), Expired(-1ns);
  record(Zero.waitForNextAttempt());
  record(Expired.waitForNextAttempt());
#ifdef BACKOFF_CONTROLLED
  struct sigaction Action = {};
  Action.sa_handler = handler;
  sigemptyset(&Action.sa_mask);
  record(sigaction(SIGUSR1, &Action, nullptr));
  auto Start = steady_clock::now();
  llvm::ExponentialBackoff Backoff(2500ms, 1s, 1s);
  char Marker[] = "__BACKOFF_WAIT_1__\n";
  for (unsigned I = 0; I < 3; ++I) {
    Marker[15] = '1' + I;
    write(2, Marker, sizeof(Marker) - 1);
    auto Before = steady_clock::now();
    record(Backoff.waitForNextAttempt());
    record(duration_cast<nanoseconds>(steady_clock::now() - Before).count());
  }
  record(Backoff.waitForNextAttempt());
  record(Signals);
  record(duration_cast<nanoseconds>(steady_clock::now() - Start).count());
#else
  auto Before = steady_clock::now();
  llvm::ExponentialBackoff Backoff(5s, 20ms, 20ms);
  record(Backoff.waitForNextAttempt());
  record(duration_cast<nanoseconds>(steady_clock::now() - Before).count());
#endif
  return write(1, Output, Count * sizeof(Output[0])) !=
         (ssize_t)(Count * sizeof(Output[0]));
}
