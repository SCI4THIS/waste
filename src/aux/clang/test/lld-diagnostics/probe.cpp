// Record actual LLD diagnostics/context/cleanup; WAST owns all expectations.
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstdlib>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

using namespace llvm;
static void record(int64_t value) { write(1, &value, sizeof(value)); }
static unsigned Cleanups, ArenaDestroyed;
static void save(const char *path, StringRef data) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  record(fd >= 0);
  record(fd < 0 ? -1 : write(fd, data.data(), data.size()));
  record(fd < 0 ? -1 : close(fd));
}
struct ArenaOwner {
  ~ArenaOwner() { ++ArenaDestroyed; }
};
static int CleanupFD = -1;
struct ManagedOwner {
  ~ManagedOwner() { write(CleanupFD, "managed\n", 8); }
};
static ManagedStatic<ManagedOwner> Managed;
static void staticExit() { write(CleanupFD, "atexit\n", 7); }

static void child(int mode) {
  CleanupFD = open(mode ? "/tmp/lld-limit-cleanup.txt" : "/tmp/lld-fatal-cleanup.txt",
                   O_WRONLY | O_CREAT | O_TRUNC, 0600);
  (void)*Managed;
  atexit(staticExit);
  int fd = open(mode ? "/tmp/lld-limit-exit.txt" : "/tmp/lld-fatal-exit.txt",
                O_WRONLY | O_CREAT | O_TRUNC, 0600);
  raw_fd_ostream output(fd, false);
  auto *ctx = new lld::CommonLinkerContext;
  auto &e = ctx->e;
  e.initialize(output, output, true, false);
  e.logName = "wasm-ld";
  e.cleanupCallback = [] { write(CleanupFD, "context\n", 8); };
  e.message("child stdout", e.outs());
  if (mode) {
    e.errorLimit = 1;
    e.error("first");
    e.error("second");
  } else {
    e.fatal("corrupt input");
  }
  // An unexpected return is a guest exit recorded by the parent, not a C check.
  _exit(99);
}

extern "C" int main(int, char **, char **) {
  record(LLVM_WASTE_GUEST); record(LLVM_ENABLE_THREADS); record(_LIBCPP_HAS_LOCALIZATION);
  record(lld::hasContext());
  SmallString<0> out, err;
  raw_svector_ostream stdoutOS(out), stderrOS(err);
  auto *ctx = new lld::CommonLinkerContext;
  record(lld::hasContext());
  auto &e = ctx->e;
  e.initialize(stdoutOS, stderrOS, false, false);
  e.logName = "wasm-ld";
  record(&lld::errorHandler() == &e);
  record(e.errorCount); record(e.errorLimit);
  e.log("hidden");
  e.message("ready", e.outs());
  e.verbose = true; e.log("details");
  e.warn("careful"); record(e.errorCount);
  e.suppressWarnings = true; e.warn("suppressed");
  e.fatalWarnings = true; e.warn("promoted"); record(e.errorCount);
  e.fatalWarnings = false; e.suppressWarnings = false;
  e.error("missing"); e.error("multi\nline"); e.warn("after");
  e.message(StringRef("a\0b", 3), e.outs());
  lld::checkError(e, createStringError(inconvertibleErrorCode(), "owned failure"));
  e.error("tagged error", lld::ErrorTag::SymbolNotFound, {});
  record(e.errorCount); e.flushStreams();
  save("/tmp/lld-normal.stdout", out); save("/tmp/lld-normal.stderr", err);

  out.clear(); err.clear(); e.errorCount = 0; e.errorLimit = 2;
  for (const char *text : {"A", "B", "C", "D"}) e.error(text);
  record(e.errorCount);
  e.warn("still warn");
  e.errorLimit = 0; e.error("E"); record(e.errorCount);
  e.errorLimit = 2; e.errorCount = 2; e.errorLimitExceededMsg = "limit reached";
  e.error("F"); record(e.errorCount);
  e.disableOutput = true;
  e.log("hidden"); e.message("hidden", e.outs()); e.warn("hidden"); e.error("hidden");
  record(e.errorCount); record(out.size());
  e.disableOutput = false;
  save("/tmp/lld-limits.stderr", err);

  err.clear(); e.errorCount = 0; e.errorLimit = 0; e.vsDiagnostics = true;
  for (const char *text : {
      "undefined symbol: x\n>>> referenced by a.o(src/a.c:12)",
      "undefined hidden symbol: x\n>>> referenced by src/b.c:34",
      "undefined symbol: x\n>>> referenced by archive member:",
      "duplicate symbol: x\n>>> defined in one.o\n>>> defined in two.o",
      "duplicate symbol: x\n>>> defined at a.o(src/c.c:56)",
      "duplicate symbol: x\n>>> defined at src/d.c:78",
      "relocation overflow\n>>> defined in x.o\n>>> referenced by b.o(src/e.c:90)",
      "relocation overflow\n>>> defined in x.o\n>>> referenced by src/f.c:11",
      "src/g.c:22: unclosed quote",
      "unrecognized location",
      "duplicate symbol: split\n>>> defined at src/one.c:10\n>>> first definition\n"
      ">>> defined at src/two.c:20\n>>> second definition"})
    e.error(text);
  record(e.errorCount);
  save("/tmp/lld-vs.stderr", err);

  err.clear(); e.vsDiagnostics = false; e.errorCount = 0;
  { lld::SyncStream s(e, lld::DiagLevel::None); s.os << "ignored"; }
  { lld::SyncStream s(e, lld::DiagLevel::Log); s.os << "sync log"; record(s.tell()); }
  { lld::SyncStream s(e, lld::DiagLevel::Msg); s.os << "sync message"; }
  { lld::SyncStream s(e, lld::DiagLevel::Warn); s.os << "sync warning"; }
  { lld::SyncStream s(e, lld::DiagLevel::Err); s.os << "sync error"; }
  { lld::SyncStream first(e, lld::DiagLevel::Err); first.os << "moved error";
    lld::SyncStream moved(std::move(first)); record(moved.str().size()); }
  record(e.errorCount);
  save("/tmp/lld-sync.stdout", out); save("/tmp/lld-sync.stderr", err);

  err.clear(); e.errorCount = 0;
  e.errorHandlingScript = "/usr/bin/false";
  e.error("script source", lld::ErrorTag::LibNotFound, {"missing.a"});
  record(e.errorCount);
  save("/tmp/lld-script.stderr", err);
  e.errorHandlingScript = {};

  // Real fatal handling discards the output, preserving its live memory, and
  // returns through the already accepted LLVM/engine recovery path.
  auto buffer = FileOutputBuffer::create("/tmp/lld-uncommitted.wasm", 16);
  record(bool(buffer));
  if (buffer) {
    e.outputBuffer = std::move(*buffer);
    e.outputBuffer->getBufferStart()[0] = 0x5a;
    record(e.outputBuffer->getBufferSize());
  } else {
    consumeError(buffer.takeError()); record(-1);
  }
  lld::make<ArenaOwner>();
  record(ArenaDestroyed);
  ctx->e.cleanupCallback = [] {
    ++Cleanups;
    record(lld::hasContext()); record(ArenaDestroyed);
    const char marker[] = "__LLD_CLEANUP__\n";
    write(2, marker, sizeof(marker) - 1);
    record(usleep(1000));
  };
  err.clear(); e.errorCount = 0; e.errorLimit = 0;
  CrashRecoveryContext::Enable();
  {
    CrashRecoveryContext recovery;
    record(recovery.RunSafely([&] { e.fatal("bad module"); }));
    record(recovery.RetCode);
  }
  record(lld::hasContext()); record(Cleanups);
  record(e.outputBuffer ? e.outputBuffer->getBufferStart()[0] : -1);
  record(access("/tmp/lld-uncommitted.wasm", F_OK));
  save("/tmp/lld-recovery.stderr", err);
  lld::CommonLinkerContext::destroy();
  record(lld::hasContext()); record(Cleanups); record(ArenaDestroyed);
  CrashRecoveryContext::Disable();
  // Sequential contexts keep real CodeGen/MC static option initialization.
  auto *again = new lld::CommonLinkerContext;
  again->e.initialize(stdoutOS, stderrOS, false, true);
  record(lld::hasContext()); record(again->e.errorCount);
  lld::CommonLinkerContext::destroy(); record(lld::hasContext());

  for (int mode : {0, 1}) {
    pid_t pid = fork();
    if (!pid) child(mode);
    record(pid > 0);
    int status = -1;
    record(waitpid(pid, &status, 0) == pid); record(status);
    record(lld::hasContext()); record(Cleanups); record(ArenaDestroyed);
  }
  return 23;
}
