// Exercise the real private reader with actual Mach-O objects. WAST asserts.
#include "DylibReader.cpp"
#include <array>
#include <unistd.h>

static std::vector<char> image(const std::array<uint8_t, 16> &UUID,
                               bool Little, bool Wide, bool HasUUID) {
  std::vector<char> Bytes;
  auto word = [&](uint32_t Value) {
    for (unsigned I = 0; I < 4; ++I)
      Bytes.push_back(static_cast<char>(Value >> (8 * (Little ? I : 3 - I))));
  };
  word(Wide ? MachO::MH_MAGIC_64 : MachO::MH_MAGIC);
  word(Wide ? MachO::CPU_TYPE_X86_64 : MachO::CPU_TYPE_X86);
  word(MachO::CPU_SUBTYPE_I386_ALL);
  word(MachO::MH_BUNDLE);
  word(HasUUID ? 1 : 0);
  word(HasUUID ? 24 : 0);
  word(0);
  if (Wide) word(0);
  if (HasUUID) {
    word(MachO::LC_UUID); word(24);
    for (uint8_t Byte : UUID) Bytes.push_back(static_cast<char>(Byte));
  }
  return Bytes;
}

extern "C" int main(int, char **) {
  const std::array<std::array<uint8_t, 16>, 3> UUIDs{{
    {},
    {255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255},
    {0,1,2,15,16,127,128,255,171,205,239,254,32,48,64,80}}};
  std::vector<std::string> Output;
  for (unsigned Case = 0; Case < 4; ++Case) {
    for (bool Little : {true, false}) {
      for (bool Wide : {false, true}) {
        auto Bytes = image(UUIDs[Case % 3], Little, Wide, Case != 3);
        auto Object = MachOObjectFile::create(
          MemoryBufferRef(StringRef(Bytes.data(), Bytes.size()), "uuid-probe"),
          Little, Wide);
        if (!Object) { errs() << toString(Object.takeError()) << '\n'; return 5; }
        RecordsSlice Slice(Triple("x86_64-apple-macos"));
        if (auto Error = readMachOHeader(Object->get(), Slice)) {
          errs() << toString(std::move(Error)) << '\n'; return 6;
        }
        Output.push_back(Slice.getBinaryAttrs().UUID.str());
      }
    }
  }
  // All parsed images, objects, slices and format streams have been destroyed.
  (void)usleep(1000);
  uint64_t Count = Output.size();
  (void)write(1, &Count, sizeof Count);
  for (const auto &Text : Output) {
    uint64_t Length = Text.size();
    (void)write(1, &Length, sizeof Length);
  }
  for (const auto &Text : Output) (void)write(1, Text.data(), Text.size());
  return 36;
}
