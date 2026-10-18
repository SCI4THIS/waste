// Observe the actual context owner and caches; WAST owns the expectations.
// Including the source also exposes its private state implementations without
// adding test hooks or replacing any production method.
#include "DWARFContext.cpp"
#include "llvm/MC/MCPseudoProbe.h"
#include <fcntl.h>
#include <type_traits>
#include <unistd.h>

static_assert(!std::is_copy_constructible<ThreadSafeState>::value);
static void record(int64_t value) { write(1, &value, sizeof(value)); }

static constexpr char Abbrev[] =
    "\x01\x11\x00\x03\x08\x11\x01\x12\x06\x00\x00\x00";
static constexpr char Info[] =
    "\x17\x00\x00\x00\x04\x00\x00\x00\x00\x00\x04\x01"
    "unit.c\x00\x00\x10\x00\x00\x20\x00\x00\x00";
static constexpr char DWOInfo[] =
    "\x18\x00\x00\x00\x04\x00\x00\x00\x00\x00\x04\x01"
    "split.c\x00\x00\x20\x00\x00\x30\x00\x00\x00";

struct Observations {
  unsigned InfoScans = 0, DWOScans = 0, AbbrevReads = 0;
  unsigned Destroyed = 0, Errors = 0;
  bool NestedAbbrev = false;
  int SleepResult = -1;
};

// Supply bytes through LLVM's normal client interface; parsing/cache methods
// all come from the actual context, unit, DIE and abbreviation sources.
class Sections final : public DWARFObject {
  Observations &O;
  bool Empty;
  DWARFSection Normal, Split;

public:
  DWARFContext *Context = nullptr;
  Sections(Observations &O, bool Empty = false) : O(O), Empty(Empty) {
    Normal.Data = StringRef(Info, sizeof(Info) - 1);
    Split.Data = StringRef(DWOInfo, sizeof(DWOInfo) - 1);
  }
  ~Sections() override { ++O.Destroyed; }
  bool isLittleEndian() const override { return true; }
  uint8_t getAddressSize() const override { return 4; }
  void forEachInfoSections(function_ref<void(const DWARFSection &)> F) const override {
    ++O.InfoScans;
    if (Empty)
      return;
    if (O.InfoScans == 1) {
      O.NestedAbbrev = Context->getDebugAbbrev() != nullptr;
      O.SleepResult = usleep(2000);
    }
    F(Normal);
  }
  void forEachInfoDWOSections(function_ref<void(const DWARFSection &)> F) const override {
    ++O.DWOScans;
    if (!Empty)
      F(Split);
  }
  StringRef getAbbrevSection() const override {
    ++O.AbbrevReads;
    return StringRef(Abbrev, sizeof(Abbrev) - 1);
  }
  StringRef getAbbrevDWOSection() const override {
    return StringRef(Abbrev, sizeof(Abbrev) - 1);
  }
  std::optional<RelocAddrEntry> find(const DWARFSection &, uint64_t) const override {
    return std::nullopt;
  }
};

extern "C" int main(int, char **, char **) {
  record(LLVM_WASTE_GUEST);                                     // 0
  record(LLVM_ENABLE_THREADS);                                  // 1
  Observations O;
  std::string names;
  raw_string_ostream names_stream(names);
  auto handle = [&](Error E) { ++O.Errors; consumeError(std::move(E)); };
  {
    auto object = std::make_unique<Sections>(O);
    Sections *sections = object.get();
    DWARFContext context(std::move(object), "", handle, handle, true);
    sections->Context = &context;
    record(context.getNumCompileUnits());                       // 2
    record(O.InfoScans);                                        // 3
    record(O.NestedAbbrev);                                     // 4
    record(O.SleepResult);                                      // 5
    auto *unit = context.getUnitAtIndex(0);
    auto die = unit->getUnitDIE();
    record(unit->getVersion());                                 // 6
    record(unit->getAddressByteSize());                         // 7
    record(die.getTag());                                       // 8
    record(toAddress(die.find(DW_AT_low_pc)).value_or(0));        // 9
    record(toUnsigned(die.find(DW_AT_high_pc)).value_or(0));      // 10
    names_stream << die.getName(DINameKind::ShortName) << '\n';
    const auto *normal = &context.getNormalUnitsVector();
    record(normal == &context.getNormalUnitsVector());          // 11
    record(unit == context.getUnitAtIndex(0));                  // 12
    record(O.InfoScans);                                        // 13
    record(context.getDebugAbbrev() == context.getDebugAbbrev());// 14
    record(O.AbbrevReads);                                      // 15
    record(context.getNumDWOCompileUnits());                    // 16
    record(O.DWOScans);                                         // 17
    auto *split = context.getDWOUnitAtIndex(0);
    auto split_die = split->getUnitDIE();
    record(split->getVersion());                                // 18
    record(toAddress(split_die.find(DW_AT_low_pc)).value_or(0));  // 19
    record(toUnsigned(split_die.find(DW_AT_high_pc)).value_or(0));// 20
    names_stream << split_die.getName(DINameKind::ShortName) << '\n';
    record(split == context.getDWOUnitAtIndex(0));               // 21
    record(&context.getCUIndex() == &context.getCUIndex());      // 22
    record(&context.getTUIndex() == &context.getTUIndex());      // 23
    record(context.getDebugLoc() == context.getDebugLoc());     // 24
    const auto *aranges = context.getDebugAranges();
    record(aranges == context.getDebugAranges());               // 25
    record(aranges->findAddress(0x1000));                        // 26
    record(aranges->findAddress(0x1020));                        // 27
    record(context.getDebugMacinfo() == context.getDebugMacinfo());// 28
    std::string dwp;
    ThreadSafeState safe(context, dwp);
    record(safe.isThreadSafe());                                // 29
    auto &eager = safe.getDWOUnits(true);
    record(eager.size());                                       // 30
    record(eager[0].get() == safe.getDWOUnits(true)[0].get());    // 31
    names_stream << eager[0]->getUnitDIE().getName(DINameKind::ShortName) << '\n';
    record(safe.getTypeUnitMap(false).size());                   // 32
    record(safe.getTypeUnitMap(true).size());                    // 33
    ThreadUnsafeDWARFContextState unsafe(context, dwp);
    record(unsafe.isThreadSafe());                              // 34
    record(unsafe.getDWOUnits(true).size());                     // 35
    record(unsafe.getDWOUnits(false).size());                    // 36
    record(O.Errors);                                          // 37
  }
  record(O.Destroyed);                                          // 38
  {
    auto object = std::make_unique<Sections>(O, true);
    DWARFContext fresh(std::move(object), "", handle, handle, true);
    record(fresh.getNumCompileUnits());                         // 39
    record(fresh.getNumDWOCompileUnits());                      // 40
    record(fresh.getDebugMacinfo() != nullptr);                 // 41
    record(O.Errors);                                          // 42
  }
  record(O.Destroyed);                                          // 43
  int fd = open("/tmp/dwarf-context-names.txt", O_WRONLY | O_CREAT | O_TRUNC, 0600);
  record(fd >= 0);                                              // 44
  record(fd < 0 ? -1 : write(fd, names.data(), names.size()));    // 45
  record(fd < 0 ? -1 : close(fd));                              // 46
  std::string outer = "outer", inner = "inner-with-owned-name";
  GUIDProbeFunctionMap functions;
  functions.emplace_back(1, 0, outer);
  functions.emplace_back(2, 0, inner);
  MCDecodedPseudoProbeInlineTree root;
  MCDecodedPseudoProbeInlineTree top({1, 99}, &root);
  MCDecodedPseudoProbeInlineTree middle({2, 0}, &top);
  MCDecodedPseudoProbeInlineTree leaf({3, UINT32_MAX}, &middle);
  MCDecodedPseudoProbe empty(0, 1, PseudoProbeType::Block, 0, 0, &top);
  MCDecodedPseudoProbe one(0, 1, PseudoProbeType::Block, 0, 0, &middle);
  MCDecodedPseudoProbe nested(0, 1, PseudoProbeType::Block, 0, 0, &leaf);
  std::string formatted;
  raw_string_ostream stream(formatted);
  stream << "empty=[" << empty.getInlineContextStr(functions) << "]\n";
  stream << "outer=[" << one.getInlineContextStr(functions) << "]\n";
  std::string retained = nested.getInlineContextStr(functions);
  stream << "nested=[" << retained << "]\n";
  outer.assign("after");
  inner.assign("other");
  functions[0].FuncName = outer;
  functions[1].FuncName = inner;
  stream << "retained=[" << retained << "]\n";
  stream << "changed=[" << nested.getInlineContextStr(functions) << "]\n";
  fd = open("/tmp/dwarf-context-pseudo.txt", O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd >= 0) {
    write(fd, formatted.data(), formatted.size());
    close(fd);
  }
  return 29;
}
