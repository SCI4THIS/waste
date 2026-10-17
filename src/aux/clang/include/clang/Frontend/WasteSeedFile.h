// Read the first layout seed line through the existing shared guest libc.
#ifndef WASTE_CLANG_FRONTEND_SEED_FILE_H
#define WASTE_CLANG_FRONTEND_SEED_FILE_H
#include "clang/Frontend/WasteFileInput.h"
namespace clang::detail {
inline bool wasteReadSeedFile(const char *Path, std::string &Seed) {
  WasteFileInput Input(Path);
  if (!Input.is_open()) return false;
  Input.readLine(Seed);
  return true;
}
} // namespace clang::detail
#endif
