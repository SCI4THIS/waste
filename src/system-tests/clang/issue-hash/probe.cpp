// Observe production token normalization and hashing; WAST owns expectations.
#include "IssueHash.cpp"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include <vector>
#include <unistd.h>

struct Input { std::string Text; unsigned Line; };
extern "C" int main(int, char **) {
  const Input Inputs[] = {
    {"  int value = 42;  // ignored\nnext();\n", 1},
    {R"(const char *s = "a b\\c";)", 1},
    {"/*lead*/ alpha /*gap*/ + beta;\nlast;\n", 1},
    {"int first;\n  int second;\nthird;\n", 2},
    {"  \t \nnext ( 3 );\nlast;\n", 1},
    {"return 37;", 1},
    {"  " + std::string(256, 'L') + " ( ) ;\n", 1}
  };
  std::vector<std::string> Output;
  for (const auto &Input : Inputs) {
    DiagnosticOptions DiagOptions;
    IntrusiveRefCntPtr<DiagnosticIDs> IDs(new DiagnosticIDs);
    DiagnosticsEngine Diags(IDs, DiagOptions, new IgnoringDiagConsumer, true);
    FileSystemOptions FSOptions;
    FileManager Files(FSOptions);
    SourceManager Sources(Diags, Files);
    auto FID = Sources.createFileID(llvm::MemoryBuffer::getMemBufferCopy(
        Input.Text, "issue-input.c"));
    Sources.setMainFileID(FID);
    LangOptions Options;
    Options.CPlusPlus = true;
    FullSourceLoc Loc(Sources.translateLineCol(FID, Input.Line, 1), Sources);
    auto Line = NormalizeLine(Sources, Loc, Options);
    Output.push_back(Line);
    Output.push_back(GetMD5HashOfContent(Line).str().str());
  }
  // Copies survive source managers, lexers, diagnostics and shared sleep.
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
