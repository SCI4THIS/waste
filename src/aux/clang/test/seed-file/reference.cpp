// Observe the original production ifstream/getline behavior on the native OS.
#include "observations.h"
#include <fstream>
int main(int Argc, char **Argv) {
  if (Argc != 2) return 2;
  std::string Root = Argv[1];
  auto Cases = seedCases();
  writeSeedInputs(Root, Cases);
  std::vector<SeedObservation> Values;
  for (unsigned I = 0; I < Cases.size(); ++I) {
    std::string Seed = "old-seed";
    std::ifstream File(seedPath(Root, I));
    bool Opened = File.is_open();
    std::getline(File, Seed);
    Values.push_back({Opened, std::move(Seed)});
  }
  emitSeedObservations(Values);
}
