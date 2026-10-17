#include "clang/Frontend/WasteSeedFile.h"
#include "observations.h"
extern "C" int main(int, char **) {
  std::string Root = "/tmp/seed-file-inputs";
  auto Cases = seedCases();
  writeSeedInputs(Root, Cases);
  std::vector<SeedObservation> Values;
  for (unsigned I = 0; I < Cases.size(); ++I) {
    std::string Seed = "old-seed";
    bool Opened = clang::detail::wasteReadSeedFile(seedPath(Root, I).c_str(), Seed);
    Values.push_back({Opened, std::move(Seed)});
  }
  (void)::usleep(1000); // Strings outlive closed files and freed getline buffers.
  emitSeedObservations(Values);
  return 36;
}
