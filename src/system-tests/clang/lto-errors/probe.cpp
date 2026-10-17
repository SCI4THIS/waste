// Observe the actual ThinBackendProc owner; all expectations belong to WAST.
#include "llvm/LTO/LTO.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/raw_ostream.h"
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

using namespace llvm;
static unsigned LiveTokens, LiveErrors, Calls, Handled;
static volatile sig_atomic_t Signals;
static void record(int64_t value) { write(1, &value, sizeof(value)); }
static void signal_handler(int) { ++Signals; }

struct Token {
  unsigned Id;
  explicit Token(unsigned id) : Id(id) { ++LiveTokens; }
  ~Token() { record(2000 + Id); --LiveTokens; }
};

class ObservedError : public ErrorInfo<ObservedError> {
public:
  static char ID;
  unsigned Id;
  std::string Message;
  unique_function<void()> OnDestroy;
  ObservedError(unsigned id, std::string message,
                unique_function<void()> on_destroy = nullptr)
      : Id(id), Message(std::move(message)), OnDestroy(std::move(on_destroy)) {
    ++LiveErrors;
  }
  ~ObservedError() override {
    record(1000 + Id); --LiveErrors;
    if (OnDestroy) OnDestroy();
  }
  void log(raw_ostream &os) const override {
    os << Id << ":" << StringRef(Message) << '\n';
  }
  std::error_code convertToErrorCode() const override {
    return std::make_error_code(std::errc::invalid_argument);
  }
};
char ObservedError::ID;

class Backend : public lto::ThinBackendProc {
public:
  using MutexType = decltype(ErrMu);
  using lto::ThinBackendProc::ThinBackendProc;
  using lto::ThinBackendProc::addError;
  ~Backend() override { record(3000); }
  template <typename F> void submit(F &&fn) {
    BackendThreadPool.async(std::forward<F>(fn));
  }
  void notify(const std::string &path) { if (OnWrite) OnWrite(path); }
  void context(const lto::Config &config, const ModuleSummaryIndex &index,
               const DenseMap<StringRef, GVSummaryMapTy> &summaries) {
    record(&Conf == &config); record(&CombinedIndex == &index);
    record(&ModuleToDefinedGVSummaries == &summaries);
    record(BackendThreadPool.getMaxConcurrency());
  }
  Error start(unsigned, BitcodeModule, const FunctionImporter::ImportMapTy &,
              const FunctionImporter::ExportSetTy &,
              const std::map<GlobalValue::GUID, GlobalValue::LinkageTypes> &,
              MapVector<StringRef, BitcodeModule> &) override {
    report_fatal_error("the ownership observer does not compile bitcode");
  }
};
static_assert(std::is_same<Backend::MutexType, sys::SmartMutex<true>>::value);
static_assert(std::is_same<DefaultThreadPool, SingleThreadExecutor>::value);
static_assert(!std::is_copy_constructible<Backend>::value);

static void save(const char *path, StringRef data) {
  int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
  record(fd >= 0);
  record(fd < 0 ? -1 : write(fd, data.data(), data.size()));
  record(fd < 0 ? -1 : close(fd));
}
static void consume(Error error, const char *path) {
  SmallString<0> text;
  raw_svector_ostream os(text);
  handleAllErrors(std::move(error), [&](const ObservedError &info) {
    record(info.Id); record(info.Message.size()); record(LiveErrors);
    info.log(os); ++Handled;
  });
  save(path, text); record(LiveErrors); record(Handled);
}

extern "C" int main(int, char **, char **) {
  record(LLVM_WASTE_GUEST); record(LLVM_ENABLE_THREADS); record(_LIBCPP_HAS_THREADS);
  lto::Config config;
  ModuleSummaryIndex index(false);
  DenseMap<StringRef, GVSummaryMapTy> summaries;
  ThreadPoolStrategy strategy; strategy.ThreadsRequested = 1;
  {
    Backend empty(config, index, summaries, nullptr, false, strategy);
    empty.context(config, index, summaries);
    record(bool(empty.wait())); record(bool(empty.wait()));
    record(LiveTokens); record(LiveErrors); record(Calls);
  }

  // Deferred captures and the stored index callback survive caller mutation.
  Error single = Error::success();
  SmallString<0> notifications;
  raw_svector_ostream notification_os(notifications);
  {
    std::string prefix = "index:";
    lto::IndexWriteCallback callback =
        [prefix, token = std::make_shared<Token>(101), &notification_os]
        (const std::string &path) {
          record(token->Id); notification_os << prefix << path << '\n';
        };
    Backend backend(config, index, summaries, callback, false, strategy);
    callback = nullptr; prefix.assign("changed:");
    std::string message = "captured backend";
    backend.submit([&, message, token = std::make_unique<Token>(11)] {
      ++Calls; record(token->Id); record(LiveTokens);
      backend.addError(make_error<ObservedError>(1, message));
      record(LiveErrors);
    });
    message.assign("changed input"); backend.notify("first.bc");
    record(Calls); record(LiveTokens); record(LiveErrors);
    single = backend.wait();
    record(bool(single)); record(Calls); record(LiveTokens); record(LiveErrors);
  }
  record(LiveTokens); record(LiveErrors);
  consume(std::move(single), "/tmp/lto-single.txt");
  save("/tmp/lto-notify.txt", notifications);

  // Real queued FIFO work, nested submission and both ErrorList join shapes.
  Error joined = Error::success();
  {
    Backend backend(config, index, summaries, nullptr, false, strategy);
    backend.submit([&, token = std::make_unique<Token>(21)] {
      ++Calls; record(token->Id);
      backend.submit([&, nested = std::make_unique<Token>(23)] {
        ++Calls; record(nested->Id);
        backend.addError(make_error<ObservedError>(5, "nested"));
      });
      backend.addError(make_error<ObservedError>(2, "first"));
    });
    backend.submit([&, token = std::make_unique<Token>(22)] {
      ++Calls; record(token->Id);
      backend.addError(joinErrors(
          make_error<ObservedError>(3, std::string("nul\0payload", 11)),
          make_error<ObservedError>(4, "last")));
    });
    record(Calls); record(LiveTokens); record(LiveErrors);
    joined = backend.wait();
    record(bool(joined)); record(Calls); record(LiveTokens); record(LiveErrors);
  }
  record(LiveErrors); consume(std::move(joined), "/tmp/lto-joined.txt");

  // Payload destruction can submit work to the still-live, drained owner.
  {
    Backend backend(config, index, summaries, nullptr, false, strategy);
    backend.submit([&, token = std::make_unique<Token>(26)] {
      ++Calls; record(token->Id);
      backend.addError(make_error<ObservedError>(6, "reentrant", [&] {
        record(6000);
        backend.submit([&, token = std::make_unique<Token>(27)] {
          ++Calls; record(token->Id);
          backend.addError(make_error<ObservedError>(7, "reused"));
        });
      }));
    });
    consume(backend.wait(), "/tmp/lto-reentrant.txt");
    record(Calls); record(LiveTokens); record(LiveErrors);
    consume(backend.wait(), "/tmp/lto-reused.txt");
    record(Calls); record(LiveTokens); record(LiveErrors);
    record(bool(backend.wait()));
  }

  // A real shared sleep suspends the callback with captures and errors live.
  {
    Backend backend(config, index, summaries, nullptr, false, strategy);
    backend.submit([&, token = std::make_unique<Token>(31)] {
      ++Calls; record(token->Id);
      backend.addError(make_error<ObservedError>(8, "before sleep"));
      record(LiveTokens); record(LiveErrors);
      record(signal(SIGUSR1, signal_handler) == SIG_ERR);
      struct timespec before = {}, after = {};
      record(clock_gettime(CLOCK_MONOTONIC, &before));
      const char marker[] = "__LTO_ERRORS_SLEEP__\n";
      write(2, marker, sizeof(marker) - 1);
      struct timespec request = {0, 10000000};
      record(nanosleep(&request, nullptr)); record(errno);
      record(Signals);
      record(LiveTokens); record(LiveErrors); record(token->Id);
      record(nanosleep(&request, nullptr));
      record(clock_gettime(CLOCK_MONOTONIC, &after));
      record((after.tv_sec - before.tv_sec) * 1000000000LL + after.tv_nsec - before.tv_nsec);
      backend.addError(make_error<ObservedError>(9, "after sleep"));
      backend.submit([&, token = std::make_unique<Token>(32)] {
        ++Calls; record(token->Id);
        backend.addError(make_error<ObservedError>(10, "after resume"));
      });
    });
    record(Calls); record(LiveTokens); record(LiveErrors);
    consume(backend.wait(), "/tmp/lto-sleep.txt");
    record(Calls); record(LiveTokens); record(LiveErrors);
    backend.addError(make_error<ObservedError>(11, "converted"));
    record(LiveErrors);
    record(errorToErrorCode(backend.wait()).value());
    record(LiveErrors); record(bool(backend.wait()));
  }
  record(Calls); record(LiveTokens); record(LiveErrors); record(Handled);
  // Actual profile context formatting needed by the real pipeline constructor.
  using namespace llvm::sampleprof;
  SmallString<0> context_text;
  raw_svector_ostream context_os(context_text);
  SampleContextFrame frame(FunctionId("frame"), LineLocation(17, 9));
  context_os << frame.toString(false) << '\n' << frame.toString(true) << '\n';
  SampleContextFrame zero(FunctionId("zero"), LineLocation(0, 0));
  context_os << zero.toString(true) << '\n';
  SampleContextFrame wide(FunctionId("wide"), LineLocation(UINT32_MAX, UINT32_MAX));
  context_os << wide.toString(true) << '\n';
  SampleContextFrame hash(FunctionId(UINT64_MAX), LineLocation(7, 0));
  context_os << hash.toString(true) << '\n';
  SampleContextFrame binary(FunctionId(StringRef("nul\0name", 8)), LineLocation(3, 4));
  context_os << binary.toString(false) << '\n' << binary.toString(true) << '\n';
  SampleContextFrame empty;
  context_os << empty.toString(false) << '\n' << empty.toString(true) << '\n';
  context_os << SampleContext::getContextString({}) << '\n';
  context_os << SampleContext::getContextString({frame}) << '\n';
  context_os << SampleContext::getContextString({frame}, true) << '\n';
  SampleContextFrame frames[] = {
      SampleContextFrame(FunctionId("caller"), LineLocation(1, 2)), frame};
  context_os << SampleContext::getContextString(frames) << '\n';
  context_os << SampleContext::getContextString(frames, true) << '\n';
  std::string owned_context = SampleContext::getContextString(frames);
  frames[0].Func = FunctionId("changed");
  context_os << owned_context << '\n' << SampleContext(frames).toString() << '\n';
  record(context_text.size()); save("/tmp/lto-contexts.txt", context_text);
  return 29;
}
