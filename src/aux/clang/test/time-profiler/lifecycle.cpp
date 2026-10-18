// Observe actual archive-backed profiler APIs; WAST owns every expectation.
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>

extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
static bool CaptureRegistry;
static void *RegistryAllocation;
static size_t RegistryBytes;
static unsigned RegistryFreed;
extern "C" void *__wrap_malloc(size_t size) {
  void *pointer = __real_malloc(size);
  if (CaptureRegistry) {
    RegistryAllocation = pointer; RegistryBytes = size; RegistryFreed = 0;
  }
  return pointer;
}
extern "C" void __wrap_free(void *pointer) {
  if (pointer && pointer == RegistryAllocation) ++RegistryFreed;
  __real_free(pointer);
}
static void record(int64_t value) { write(1, &value, sizeof(value)); }
static void shutdown_report() {
  record(990); record(RegistryAllocation != nullptr); record(RegistryBytes);
  record(RegistryFreed); record(llvm::timeTraceProfilerEnabled());
}
static void pause(const char *marker) {
  write(2, marker, __builtin_strlen(marker));
  record(usleep(1000));
}
static void save(const char *path, llvm::StringRef bytes) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  record(fd >= 0);
  record(fd < 0 ? -1 : write(fd, bytes.data(), bytes.size()));
  if (fd >= 0) record(close(fd));
  else record(-1);
}
static void finish() {
  CaptureRegistry = true;
  llvm::timeTraceProfilerFinishThread();
  CaptureRegistry = false;
  record(llvm::timeTraceProfilerEnabled()); record(RegistryBytes);
  record(RegistryFreed);
}
static void snapshot(const char *path) {
  llvm::SmallVector<char, 0> text;
  llvm::raw_svector_ostream stream(text);
  llvm::timeTraceProfilerWrite(stream);
  save(path, llvm::StringRef(text.data(), text.size()));
}

extern "C" int main(int, char **, char **) {
  record(atexit(shutdown_report));
  record(llvm::timeTraceProfilerEnabled()); record(llvm::isTimeTraceVerbose());
  unsigned details = 0;
  auto lazy_detail = [&]() { ++details; return std::string("lazy"); };
  record(llvm::timeTraceProfilerBegin("disabled", lazy_detail) != nullptr);
  llvm::timeTraceProfilerEnd();
  llvm::timeTraceAddInstantEvent("disabled", lazy_detail);
  record(details);
  llvm::timeTraceProfilerCleanup();
  record(llvm::timeTraceProfilerEnabled());

  // Sequential finished instances exercise aggregation in the disabled-thread
  // compiler profile. No threads or replacement LLVM implementation are used.
  llvm::timeTraceProfilerInitialize(0, "/usr/bin/profiler", true);
  record(llvm::timeTraceProfilerEnabled()); record(llvm::isTimeTraceVerbose());
  std::string name = "Compile", detail(64, 'd');
  auto *outer = llvm::timeTraceProfilerBegin(name, detail);
  name.assign("changed"); detail.assign("changed");
  record(outer != nullptr);
  auto *inner = llvm::timeTraceProfilerBegin("Compile", "nested");
  llvm::timeTraceAddInstantEvent("nested instant", []() { return std::string("quote\"\\\n\t"); });
  pause("__PROFILER_A__\n");
  llvm::timeTraceProfilerEnd(inner);
  llvm::TimeTraceMetadata metadata;
  metadata.Detail = "source"; metadata.File = "/root/a\".c"; metadata.Line = 17;
  auto *parse = llvm::timeTraceProfilerBegin("Parse", [&]() { return metadata; });
  metadata.Detail.assign("changed"); metadata.File.assign("changed"); metadata.Line = 99;
  pause("__PROFILER_B__\n");
  llvm::timeTraceProfilerEnd(parse); llvm::timeTraceProfilerEnd(outer);
  auto *link = llvm::timeTraceAsyncProfilerBegin("Link", "async");
  llvm::timeTraceAddInstantEvent("link instant", []() { return std::string("inside"); });
  pause("__PROFILER_C__\n");
  llvm::timeTraceProfilerEnd(link);
  finish(); record(llvm::isTimeTraceVerbose());

  llvm::timeTraceProfilerInitialize(0, "/work/second", false);
  record(llvm::isTimeTraceVerbose());
  llvm::timeTraceProfilerBegin("Compile", lazy_detail);
  pause("__PROFILER_D__\n"); llvm::timeTraceProfilerEnd();
  record(details); finish();

  llvm::timeTraceProfilerInitialize(0, "/work/driver clang", true);
  llvm::timeTraceAddInstantEvent("outside", lazy_detail); record(details);
  {
    llvm::TimeTraceScope scope("Driver", "scope");
    pause("__PROFILER_E__\n");
  }
  snapshot("/tmp/profiler-stream.json");
  record(llvm::errorToErrorCode(llvm::timeTraceProfilerWrite(
      "/tmp/profiler-preferred.json", "/tmp/profiler-unused")).value());
  record(llvm::errorToErrorCode(llvm::timeTraceProfilerWrite(
      "", "/tmp/profiler-fallback")).value());
  record(llvm::errorToErrorCode(llvm::timeTraceProfilerWrite("", "-")).value());
  record(llvm::errorToErrorCode(llvm::timeTraceProfilerWrite(
      "/tmp/profiler-missing/trace.json", "/tmp/profiler-unused")).value());
  record(RegistryFreed);
  llvm::timeTraceProfilerCleanup();
  record(llvm::timeTraceProfilerEnabled()); record(llvm::isTimeTraceVerbose());
  record(RegistryFreed);
  llvm::timeTraceProfilerCleanup(); record(llvm::timeTraceProfilerEnabled());

  llvm::timeTraceProfilerInitialize(5000, "/work/threshold");
  llvm::timeTraceProfilerBegin("Short", "filtered");
  llvm::timeTraceAddInstantEvent("filtered instant", lazy_detail);
  pause("__PROFILER_F__\n"); llvm::timeTraceProfilerEnd();
  llvm::timeTraceProfilerBegin("Zero", "filtered"); llvm::timeTraceProfilerEnd();
  record(details); snapshot("/tmp/profiler-threshold.json");
  llvm::timeTraceProfilerCleanup();
  llvm::timeTraceProfilerInitialize(0, "/work/reuse");
  snapshot("/tmp/profiler-reuse.json");
  llvm::timeTraceProfilerCleanup();
  record(RegistryFreed); record(llvm::timeTraceProfilerEnabled());
  return 17;
}
