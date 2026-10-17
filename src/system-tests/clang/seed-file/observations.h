#ifndef WASTE_SEED_FILE_OBSERVATIONS_H
#define WASTE_SEED_FILE_OBSERVATIONS_H
#include "cases.h"
#include <cstdio>
#include <cstdint>
#include <sys/stat.h>
#include <unistd.h>
struct SeedObservation { bool Opened; std::string Seed; };
inline std::string seedPath(const std::string &Root, unsigned Index) {
  return Root + "/input-" + std::to_string(Index);
}
inline void writeSeedInputs(const std::string &Root,
                            const std::vector<SeedCase> &Cases) {
  (void)::mkdir(Root.c_str(), 0700);
  for (unsigned I = 0; I < Cases.size(); ++I) {
    std::string Path = seedPath(Root, I);
    if (Cases[I].Missing) { (void)::unlink(Path.c_str()); continue; }
    if (auto *File = std::fopen(Path.c_str(), "wb")) {
      (void)std::fwrite(Cases[I].Text.data(), 1, Cases[I].Text.size(), File);
      std::fclose(File);
    }
  }
}
inline void emitSeedWord(std::uint32_t Value) {
  const unsigned char Bytes[] = {static_cast<unsigned char>(Value),
      static_cast<unsigned char>(Value >> 8),
      static_cast<unsigned char>(Value >> 16),
      static_cast<unsigned char>(Value >> 24)};
  (void)::write(1, Bytes, sizeof Bytes);
}
inline void emitSeedObservations(const std::vector<SeedObservation> &Values) {
  emitSeedWord(Values.size());
  for (const auto &V : Values) {
    emitSeedWord(V.Opened);
    emitSeedWord(V.Seed.size());
  }
  for (const auto &V : Values) (void)::write(1, V.Seed.data(), V.Seed.size());
}
#endif
