#include "llvm/ADT/SmallString.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFObject.h"
#include "llvm/DebugInfo/DWARF/DWARFSection.h"
#include "llvm/DebugInfo/GSYM/DwarfTransformer.h"
#include "llvm/DebugInfo/GSYM/FileWriter.h"
#include "llvm/DebugInfo/GSYM/GsymCreatorV1.h"
#include "llvm/DebugInfo/GSYM/GsymCreatorV2.h"
#include "llvm/DebugInfo/GSYM/GsymReader.h"
#include "llvm/DebugInfo/GSYM/OutputAggregator.h"
#include "llvm/Support/raw_ostream.h"
#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <type_traits>

using namespace llvm;
using namespace llvm::gsym;

namespace {
std::array<int64_t, 114> Values{};
size_t Cursor = 0;
unsigned ContextErrors = 0;

void observe(int64_t Value) {
  if (Cursor < Values.size())
    Values[Cursor++] = Value;
}

void u32(std::string &Out, uint32_t Value) {
  for (unsigned I = 0; I < 4; ++I)
    Out.push_back(static_cast<char>(Value >> (I * 8)));
}

void text(std::string &Out, StringRef Value) {
  Out.append(Value.data(), Value.size());
  Out.push_back(0);
}

std::string unit(StringRef File, StringRef Function, uint32_t Address,
                 uint32_t Size) {
  std::string Out(4, '\0');
  Out.push_back(4);
  Out.push_back(0);
  u32(Out, 0);
  Out.push_back(4);
  Out.push_back(1);
  text(Out, File);
  u32(Out, Address);
  u32(Out, Size);
  Out.push_back(dwarf::DW_LANG_C);
  Out.push_back(0);
  text(Out, "/src");
  Out.push_back(2);
  text(Out, Function);
  u32(Out, Address);
  u32(Out, Size);
  Out.push_back(1);
  Out.push_back(0);
  const uint32_t Length = Out.size() - 4;
  for (unsigned I = 0; I < 4; ++I)
    Out[I] = static_cast<char>(Length >> (I * 8));
  return Out;
}

class Sections final : public DWARFObject {
  static constexpr unsigned char Abbreviations[] = {
      1, dwarf::DW_TAG_compile_unit, dwarf::DW_CHILDREN_yes,
      dwarf::DW_AT_name, dwarf::DW_FORM_string,
      dwarf::DW_AT_low_pc, dwarf::DW_FORM_addr,
      dwarf::DW_AT_high_pc, dwarf::DW_FORM_data4,
      dwarf::DW_AT_language, dwarf::DW_FORM_data2,
      dwarf::DW_AT_comp_dir, dwarf::DW_FORM_string, 0, 0,
      2, dwarf::DW_TAG_subprogram, dwarf::DW_CHILDREN_no,
      dwarf::DW_AT_name, dwarf::DW_FORM_string,
      dwarf::DW_AT_low_pc, dwarf::DW_FORM_addr,
      dwarf::DW_AT_high_pc, dwarf::DW_FORM_data4,
      dwarf::DW_AT_external, dwarf::DW_FORM_flag, 0, 0, 0};
  std::array<std::string, 2> Data{
      unit("a.c", "alpha", 0x1000, 32), unit("b.c", "beta", 0x2000, 48)};
  std::array<DWARFSection, 2> Info;

public:
  Sections() {
    for (size_t I = 0; I < Info.size(); ++I)
      Info[I].Data = Data[I];
  }
  bool isLittleEndian() const override { return true; }
  uint8_t getAddressSize() const override { return 4; }
  void forEachInfoSections(
      function_ref<void(const DWARFSection &)> Visit) const override {
    for (const auto &Section : Info)
      Visit(Section);
  }
  StringRef getAbbrevSection() const override {
    return {reinterpret_cast<const char *>(Abbreviations),
            sizeof(Abbreviations)};
  }
  std::optional<RelocAddrEntry> find(const DWARFSection &,
                                   uint64_t) const override {
    return std::nullopt;
  }
};

bool failed(Error E) {
  if (!E)
    return false;
  consumeError(std::move(E));
  return true;
}

void name(std::string &Names, StringRef Value) {
  Names.append(Value.data(), Value.size());
  Names.push_back('\n');
}

template <typename CreatorType>
void convert(uint32_t Threads, llvm::endianness Order, std::string &Names) {
  static_assert(!std::is_copy_constructible_v<CreatorType>);
  auto Context = std::make_unique<DWARFContext>(
      std::make_unique<Sections>(), "", [](Error E) {
        ++ContextErrors;
        consumeError(std::move(E));
      }, [](Error E) {
        ++ContextErrors;
        consumeError(std::move(E));
      });
  CreatorType Creator;
  std::string Owned = "owned-name";
  const auto Offset = Creator.insertString(Owned);
  observe(Threads);
  observe(Context->getNumCompileUnits());
  observe(Offset == Creator.insertString("owned-name"));
  const auto File = Creator.insertFile("/src/unit.c");
  observe(File == Creator.insertFile("/src/unit.c"));
  observe(File);
  Owned.assign(Owned.size(), 'x');
  name(Names, Creator.getString(Offset));

  std::string Diagnostics;
  raw_string_ostream Stream(Diagnostics);
  OutputAggregator Output(&Stream);
  DwarfTransformer Transformer(*Context, Creator);
  observe(failed(Transformer.convert(Threads, Output)));
  observe(Creator.getNumFunctionInfos());
  observe(Output.GetNumCategories());
  Creator.setBaseAddress(0x1000);
  observe(failed(Creator.finalize(Output)));
  observe(Creator.getNumFunctionInfos());

  SmallString<256> Bytes;
  raw_svector_ostream Encoded(Bytes);
  FileWriter Writer(Encoded, Order);
  Writer.setStringOffsetSize(Creator.getStringOffsetSize());
  observe(failed(Creator.encode(Writer)));
  observe(!Bytes.empty());
  auto ReaderOrError = GsymReader::copyBuffer(Bytes);
  observe(!ReaderOrError);
  if (!ReaderOrError) {
    const std::string Message = toString(ReaderOrError.takeError());
    std::fprintf(stderr, "GSYM reader: %s\n", Message.c_str());
    return;
  }
  auto &Reader = **ReaderOrError;
  observe(Reader.getBaseAddress());
  observe(Reader.getVersion());
  observe(Reader.getNumAddresses());
  observe(Reader.getAddress(0).value_or(0));
  observe(Reader.getAddress(1).value_or(0));
  auto First = Reader.getFunctionInfo(0x101f);
  observe(!First);
  if (First) {
    observe(First->Range.start());
    observe(First->Range.size());
    name(Names, Reader.getString(First->Name));
  } else {
    consumeError(First.takeError());
    observe(0);
    observe(0);
  }
  auto Second = Reader.getFunctionInfo(0x2000);
  observe(!Second);
  if (Second) {
    observe(Second->Range.start());
    observe(Second->Range.size());
    name(Names, Reader.getString(Second->Name));
  } else {
    consumeError(Second.takeError());
    observe(0);
    observe(0);
  }
  auto Gap = Reader.getFunctionInfo(0x1020);
  observe(!Gap);
  if (!Gap)
    consumeError(Gap.takeError());
  auto End = Reader.getFunctionInfo(0x2030);
  observe(!End);
  if (!End)
    consumeError(End.takeError());
  for (char &Byte : Bytes)
    Byte = 0;
  observe(Reader.getVersion());
  observe(Reader.getAddress(1).value_or(0));
}
} // namespace

extern "C" int main(int, char **, char **) {
  std::string Names;
  convert<GsymCreatorV1>(1, llvm::endianness::little, Names);
  convert<GsymCreatorV1>(3, llvm::endianness::big, Names);
  convert<GsymCreatorV2>(1, llvm::endianness::little, Names);
  convert<GsymCreatorV2>(3, llvm::endianness::big, Names);
  observe(ContextErrors);
  FILE *Output = std::fopen("/tmp/gsym-names.txt", "wb");
  observe(Output ? std::fwrite(Names.data(), 1, Names.size(), Output) : -1);
  if (Output)
    std::fclose(Output);
  std::fwrite(Values.data(), sizeof(Values[0]), Cursor, stdout);
  return 31;
}
