// Observations only: WAST owns expectations, including all 256 suffix bytes.
#include "cases.h"
inline std::vector<unsigned char> spirvNameObservations() {
  std::vector<unsigned char> Bytes;
  for (const auto &Input : spirvNameInputs())
    Bytes.push_back(isValidSPIRVExtensionName(Input));
  for (unsigned I = 0; I < 256; ++I)
    Bytes.push_back(isValidSPIRVExtensionName("SPV_" + std::string(1, static_cast<char>(I))));
  return Bytes;
}
