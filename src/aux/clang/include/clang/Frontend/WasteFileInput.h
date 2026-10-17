// Private line input for Clang's localization-free guest C++ profile.
#ifndef WASTE_CLANG_FRONTEND_FILE_INPUT_H
#define WASTE_CLANG_FRONTEND_FILE_INPUT_H
#include <cstdio>
#include <cstdlib>
#include <string>

namespace clang::detail {
class WasteFileInput {
  std::FILE *File;
  char *Data = nullptr;
  std::size_t Capacity = 0;
  bool Good;
public:
  explicit WasteFileInput(const char *Path)
      : File(std::fopen(Path, "r")), Good(File != nullptr) {}
  WasteFileInput(const WasteFileInput &) = delete;
  WasteFileInput &operator=(const WasteFileInput &) = delete;
  ~WasteFileInput() {
    std::free(Data);
    if (File) std::fclose(File);
  }
  bool is_open() const { return File != nullptr; }
  bool good() const { return Good; }
  bool readLine(std::string &Line) {
    if (!Good) return false;
    Line.clear();
    auto Length = ::getline(&Data, &Capacity, File);
    if (Length < 0) { Good = false; return false; }
    // Remove only LF; keep spaces, CR and embedded NUL bytes.
    if (Length && Data[Length - 1] == '\n') --Length;
    Line.assign(Data, static_cast<std::size_t>(Length));
    Good = !std::feof(File) && !std::ferror(File);
    return true;
  }
};
} // namespace clang::detail
#endif
