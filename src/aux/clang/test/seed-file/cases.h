#ifndef WASTE_SEED_FILE_CASES_H
#define WASTE_SEED_FILE_CASES_H
#include <string>
#include <vector>
struct SeedCase { bool Missing; std::string Text; };
inline std::vector<SeedCase> seedCases() {
  return {{true, ""}, {false, ""}, {false, "seed\nignored\n"},
          {false, "  leading \t trailing \r\nrest"},
          {false, std::string("ab\0cd\nignored", 13)},
          {false, "last-line"}, {false, "\nignored"},
          {false, std::string(4097, 'L') + "\nignored"}};
}
#endif
