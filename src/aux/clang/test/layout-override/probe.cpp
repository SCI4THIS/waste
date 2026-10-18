// Observe the actual production layout parser. Only the test has private access.
#include "clang/Frontend/LayoutOverrideSource.h"
#include <cstdio>
#include <cstdint>
#include <string>
#include <unistd.h>
#include <vector>
using namespace clang;
using Words = std::vector<std::uint64_t>;
static void file(const char *Path, const std::string &Text) {
  if (auto *F = std::fopen(Path, "wb")) {
    (void)std::fwrite(Text.data(), 1, Text.size(), F);
    std::fclose(F);
  }
}
static void record(const LayoutOverrideSource &Source, const std::string &Name,
                   Words &Output) {
  auto It = Source.Layouts.find(Name);
  Output.push_back(It != Source.Layouts.end());
  if (It == Source.Layouts.end()) return;
  const auto &L = It->second;
  Output.push_back(L.Size); Output.push_back(L.Align);
  Output.push_back(L.FieldOffsets.size());
  for (auto V : L.FieldOffsets) Output.push_back(V);
  Output.push_back(L.BaseOffsets.size());
  for (auto V : L.BaseOffsets) Output.push_back(V.getQuantity());
  Output.push_back(L.VBaseOffsets.size());
  for (auto V : L.VBaseOffsets) Output.push_back(V.getQuantity());
}
extern "C" int main(int, char **) {
  const std::string LongName = std::string(256, 'L') + "_7";
  const std::string Text = std::string(5000, 'x') + "\n"
      "*** Dumping AST Record Layout\nType: struct Plain\n Size:128\n"
      "Alignment:64\nFieldOffsets: [0, 64]\nBaseOffsets: [0, 16]\n"
      "VBaseOffsets: [32]\n"
      "*** Dumping AST Record Layout\r\nType: class Bytes\r\n"
      "  [sizeof=24, align=8]\r\nFieldOffsets: [0, 32, 64]\r\n"
      "*** Dumping AST Record Layout\nType: union Union\n Size:64\n"
      "Alignment:32\nFieldOffsets: [0, 0, 0]\n"
      "*** Dumping AST Record Layout\nType: struct " + LongName +
      "\n Size:1099511627776\nAlignment:128\nFieldOffsets: [0, 1099511627776]\n"
      "*** Dumping AST Record Layout\nType: struct (anonymous at input:1:1)\n"
      " Size:32\nAlignment:32\nFieldOffsets: [0]\n"
      "*** Dumping AST Record Layout\nType: struct Plain\n Size:256\n"
      "Alignment:128\nFieldOffsets: [0, 128]\nBaseOffsets: [32]\n"
      "VBaseOffsets: [64, 96]"; // No final LF: the last record must still flush.
  file("/tmp/layout-override-input.txt", Text);
  file("/tmp/layout-override-empty.txt", "");
  (void)::unlink("/tmp/layout-override-missing.txt");
  Words Output;
  {
    LayoutOverrideSource Missing("/tmp/layout-override-missing.txt");
    LayoutOverrideSource Empty("/tmp/layout-override-empty.txt");
    Output.push_back(Missing.Layouts.size());
    Output.push_back(Empty.Layouts.size());
    LayoutOverrideSource Source("/tmp/layout-override-input.txt");
    Output.push_back(Source.Layouts.size());
    for (const auto &Name : {std::string("Plain"), std::string("Bytes"),
                             std::string("Union"), LongName, std::string("NotThere")})
      record(Source, Name, Output);
  }
  (void)::usleep(1000); // Parser objects and input buffers no longer exist.
  for (std::uint64_t V : Output) {
    unsigned char Data[8];
    for (unsigned I = 0; I < 8; ++I) Data[I] = static_cast<unsigned char>(V >> (8 * I));
    (void)::write(1, Data, sizeof Data);
  }
  return 36;
}
