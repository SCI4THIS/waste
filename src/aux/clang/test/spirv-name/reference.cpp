#include WASTE_CHECK_SOURCE
#include "observations.h"
#include <cstdio>
int main() {
  const auto Bytes = spirvNameObservations();
  return std::fwrite(Bytes.data(), 1, Bytes.size(), stdout) == Bytes.size() ? 0 : 1;
}
