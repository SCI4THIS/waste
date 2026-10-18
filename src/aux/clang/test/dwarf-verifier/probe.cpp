// Observe actual DWARF diagnostic ownership; assertions belong in WAST.
#include "llvm/DebugInfo/DWARF/DWARFVerifier.h"
#include "llvm/Support/raw_ostream.h"
#include <fcntl.h>
#include <type_traits>
#include <unistd.h>

using namespace llvm;
static_assert(!std::is_copy_constructible<OutputCategoryAggregator>::value);
static void record(int64_t value) { write(1, &value, sizeof(value)); }
static void save(const char *path, StringRef bytes) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  record(fd >= 0);
  record(fd < 0 ? -1 : write(fd, bytes.data(), bytes.size()));
  record(fd < 0 ? -1 : close(fd));
}

extern "C" int main(int, char **, char **) {
  record(LLVM_WASTE_GUEST);                                    // 0
  record(LLVM_ENABLE_THREADS);                                 // 1
  int details = 0;
  OutputCategoryAggregator owner;
  record(owner.GetNumErrors());                                // 2
  record(owner.GetNumCategories());                            // 3
  auto detail = [&] { ++details; };
  owner.Report("omega", "cold", detail);
  record(details);                                             // 4
  record(owner.GetNumErrors());                                // 5
  record(owner.GetNumCategories());                            // 6
  owner.Report("alpha", "hot", detail);
  owner.Report("alpha", "hot", detail);
  owner.Report("alpha", "cold", detail);
  std::string category = "category-owned-beyond-small-string-storage";
  std::string subcategory = "subcategory-owned-beyond-small-string-storage";
  owner.Report(category, subcategory, detail);
  category.assign("changed-category");
  subcategory.assign("changed-subcategory");
  record(owner.GetNumErrors());                                // 7
  record(owner.GetNumCategories());                            // 8
  record(details);                                             // 9

  owner.ShowDetail(true);
  owner.Report("alpha", "hot", [&] {
    ++details;
    record(owner.GetNumErrors());                              // 10
    record(owner.GetNumCategories());                          // 11
    record(details);                                           // 12
    record(usleep(2000));                                      // 13
  });
  record(details);                                             // 14
  owner.Report("alpha", detail);
  record(details);                                             // 15
  owner.ShowDetail(false);
  owner.Report("beta", detail);
  owner.Report("", "zero", detail);
  record(owner.GetNumErrors());                                // 16
  record(owner.GetNumCategories());                            // 17
  record(details);                                             // 18

  std::string categories;
  raw_string_ostream category_stream(categories);
  unsigned enumerated = 0;
  owner.EnumerateResults([&](StringRef name, unsigned count) {
    ++enumerated;
    category_stream << name << '=' << count << '\n';
  });
  record(enumerated);                                          // 19
  unsigned missing = 0;
  owner.EnumerateDetailedResultsFor("absent", [&](StringRef, unsigned) {
    ++missing;
  });
  record(missing);                                             // 20

  std::string subcategories;
  raw_string_ostream detail_stream(subcategories);
  for (StringRef name : {StringRef("alpha"),
                        StringRef("category-owned-beyond-small-string-storage"),
                        StringRef("")}) {
    owner.EnumerateDetailedResultsFor(name, [&](StringRef sub, unsigned count) {
      detail_stream << name << '/' << sub << '=' << count << '\n';
    });
  }
  save("/tmp/dwarf-categories.txt", categories);                 // 21-23
  save("/tmp/dwarf-subcategories.txt", subcategories);           // 24-26
  {
    OutputCategoryAggregator reused(true);
    record(reused.GetNumErrors());                             // 27
    record(reused.GetNumCategories());                         // 28
    reused.Report("again", "warm", [&] {
      record(reused.GetNumErrors());                           // 29
      ++details;
    });
  }
  record(details);                                             // 30
  return 31;
}
