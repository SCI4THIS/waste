// Inspect actual entry storage, without replacing the pending static registry.
#include "TimeProfiler.cpp"
#include <cstdint>
#include <type_traits>
#include <unistd.h>

static int32_t Output[128];
static unsigned Count;
static void record(int32_t Value) { Output[Count++] = Value; }
static TimePointType point(int64_t Nanoseconds) {
  return TimePointType(std::chrono::duration_cast<DurationType>(
      std::chrono::nanoseconds(Nanoseconds)));
}

extern "C" int main(int argc, char **argv, char **environment) {
  (void)argc; (void)argv; (void)environment;
  record(LLVM_WASTE_GUEST);
  record(LLVM_ENABLE_THREADS);
  record(_LIBCPP_HAS_THREADS);
  record(std::is_same<TimeTraceMutex, llvm::sys::SmartMutex<true>>::value);
  record(std::is_copy_constructible<TimeTraceMutex>::value);
  record(std::is_move_constructible<TimeTraceMutex>::value);
  record(std::is_copy_constructible<TimeTraceProfilerEntry>::value);
  record(std::is_trivially_destructible<TimeTraceProfilerEntry>::value);

  // Exercise the registry's actual selected field and guard types locally.
  {
    TimeTraceProfilerInstances Local;
    std::lock_guard<decltype(Local.Lock)> Guard(Local.Lock);
    record(Local.List.size());
    record(Local.Lock.try_lock());
    Local.Lock.unlock();
    record(Local.List.size());
  }

  // Flame-graph conversion truncates points before subtracting, not durations.
  TimeTraceProfilerEntry Fraction(point(1001999), point(1002001),
                                  "fraction", "detail",
                                  TimeTraceEventType::CompleteEvent);
  record(Fraction.getFlameGraphStartUs(point(1000001)));
  record(Fraction.getFlameGraphDurUs());
  record(std::chrono::duration_cast<std::chrono::microseconds>(
             Fraction.End - Fraction.Start).count());
  record(Fraction.getFlameGraphStartUs(point(1002001)));
  record(static_cast<int>(Fraction.EventType));
  TimeTraceProfilerEntry Negative(point(-1999), point(1), "negative", "",
                                  TimeTraceEventType::AsyncEvent);
  record(Negative.getFlameGraphStartUs(point(0)));
  record(Negative.getFlameGraphDurUs());
  record(Negative.Metadata.isEmpty());
  record(static_cast<int>(Negative.EventType));
  TimeTraceProfilerEntry Large(point(5000000001999LL), point(5000000002001LL),
                               "large", "", TimeTraceEventType::CompleteEvent);
  record(Large.getFlameGraphStartUs(point(5000000000001LL)));
  record(Large.getFlameGraphDurUs());
  record(Large.Start.time_since_epoch().count() > INT32_MAX);

  // Actual heap-backed names/details remain owned after source mutation.
  std::string Name(160, 'N'), Detail(192, 'D');
  TimeTraceProfilerEntry Owned(point(0), point(9001), std::move(Name),
                               std::move(Detail), TimeTraceEventType::AsyncEvent);
  Name.assign("changed"); Detail.assign("changed");
  record(Owned.Name.size()); record(Owned.Name.front()); record(Owned.Name.back());
  record(Owned.Metadata.Detail.size()); record(Owned.Metadata.Detail.front());
  record(Owned.Metadata.Detail.back()); record(Owned.Metadata.isEmpty());
  record(Owned.getFlameGraphDurUs()); record(static_cast<int>(Owned.EventType));
  auto Copy = Owned;
  Copy.Metadata.Detail[0] = 'C';
  record(Owned.Metadata.Detail.front()); record(Copy.Metadata.Detail.front());
  record(Copy.Name.data() != Owned.Name.data());
  record(Copy.Metadata.Detail.data() != Owned.Metadata.Detail.data());
  const char *CopiedDetail = Copy.Metadata.Detail.data();
  auto Moved = std::move(Copy);
  record(Moved.Metadata.Detail.data() == CopiedDetail);
  record(Moved.Name.size()); record(Moved.Metadata.Detail.size());
  record(Moved.Metadata.Detail.front()); record(Owned.Metadata.Detail.front());

  // Full metadata and the source's detail/file emptiness rule.
  TimeTraceMetadata Metadata;
  record(Metadata.isEmpty());
  Metadata.Line = 7; record(Metadata.isEmpty());
  Metadata.File = "/tmp/input.c"; record(Metadata.isEmpty());
  Metadata.Detail = "metadata";
  TimeTraceProfilerEntry WithMetadata(point(0), point(1000), "meta",
                                      std::move(Metadata),
                                      TimeTraceEventType::CompleteEvent);
  record(WithMetadata.Metadata.Line); record(WithMetadata.Metadata.File.size());
  record(WithMetadata.Metadata.File.front()); record(WithMetadata.Metadata.File.back());
  record(WithMetadata.Metadata.Detail.size()); record(WithMetadata.Metadata.isEmpty());
  auto MetadataCopy = WithMetadata;
  WithMetadata.Metadata.Detail[0] = 'X';
  WithMetadata.Metadata.File[0] = 'Y';
  record(MetadataCopy.Metadata.Detail.front()); record(WithMetadata.Metadata.Detail.front());
  record(MetadataCopy.Metadata.File.front()); record(WithMetadata.Metadata.File.front());

  // In-progress instant storage grows and copies; snapshots outlive its owner.
  std::vector<TimeTraceProfilerEntry> Snapshot;
  {
    auto Parent = std::make_unique<InProgressEntry>(
        point(0), point(0), "parent", "parent detail", TimeTraceEventType::CompleteEvent);
    record(Parent->Event.Name.size()); record(Parent->Event.Metadata.Detail.size());
    record(Parent->InstantEvents.size());
    for (unsigned I = 0; I != 257; ++I)
      Parent->InstantEvents.emplace_back(
          point(I * 1000 + 999), point(0), "instant", std::string(64, 'a' + I % 26),
          TimeTraceEventType::InstantEvent);
    record(Parent->InstantEvents.size());
    for (const auto &Entry : Parent->InstantEvents)
      Snapshot.emplace_back(Entry);
    Parent->InstantEvents.front().Metadata.Detail[0] = 'Z';
    record(Snapshot.front().Metadata.Detail.front());
    record(Parent->InstantEvents.front().Metadata.Detail.front());
    record(Snapshot.front().Metadata.Detail.data() !=
           Parent->InstantEvents.front().Metadata.Detail.data());
    Parent->InstantEvents.clear(); record(Parent->InstantEvents.size());
  }
  record(Snapshot.size()); record(Snapshot.front().Name.size());
  record(Snapshot.front().Metadata.Detail.size());
  record(Snapshot.front().Metadata.Detail.front());
  record(Snapshot.back().Metadata.Detail.front());
  record(Snapshot.back().getFlameGraphStartUs(point(0)));
  record(static_cast<int>(Snapshot.back().EventType));
  auto *Storage = Snapshot.data();
  auto MovedSnapshot = std::move(Snapshot);
  record(MovedSnapshot.data() == Storage); record(MovedSnapshot.size());
  record(MovedSnapshot[128].Metadata.Detail.front());
  return write(1, Output, Count * sizeof(Output[0])) < 0;
}
