// Observe the real MC/DC record formatters; WAST owns the expectations.
#include "llvm/ProfileData/Coverage/CoverageMapping.h"
#include <unistd.h>
using namespace llvm;
using namespace llvm::coverage;
using Record = MCDCRecord;
static Record::TestVector vector(Record::CondState A, Record::CondState B,
                                 Record::CondState C) {
  Record::TestVector V(3);
  V.set(2, A); V.set(0, B); V.set(1, C);
  return V;
}
extern "C" int main(int, char **) {
  std::vector<std::string> Output;
  {
    auto Region = CounterMappingRegion::makeDecisionRegion({0, 3}, 0, 1, 1, 2, 1);
    Record::BoolVector Folded{BitVector(3), BitVector(3)};
    Folded[true].set(2);
    Record Owner(Region,
      {{vector(Record::MCDC_False, Record::MCDC_True, Record::MCDC_DontCare), Record::MCDC_False},
       {vector(Record::MCDC_True, Record::MCDC_True, Record::MCDC_DontCare), Record::MCDC_True}},
      {{vector(Record::MCDC_DontCare, Record::MCDC_False, Record::MCDC_DontCare), Record::MCDC_False}},
      std::move(Folded), {{0, 2}, {1, 0}, {2, 1}}, {{0, {4294967295U, 4294967294U}}, {1, {0, 9}}});
    Output.push_back(Owner.getTestVectorHeaderString());
    Output.push_back(Owner.getConditionHeaderString(0));
    Output.push_back(Owner.getConditionHeaderString(1));
    for (unsigned C = 0; C < 3; ++C)
      Output.push_back(Owner.getConditionCoverageString(C));
    Output.push_back(Owner.getTestVectorString(0));
    Output.push_back(Owner.getTestVectorString(1));
    Output.push_back(Owner.getNotExecutedTestVectorString(0));
    auto EmptyRegion = CounterMappingRegion::makeDecisionRegion({0, 12}, 0, 1, 1, 2, 1);
    Record Empty(EmptyRegion, {}, {}, {BitVector(12), BitVector(12)}, {}, {});
    Output.push_back(Empty.getTestVectorHeaderString());
    Record Wide(EmptyRegion, {}, {{Record::TestVector(12), Record::MCDC_False}},
                {BitVector(12), BitVector(12)}, {}, {});
    Output.push_back(Wide.getTestVectorHeaderString());
  }
  // Every string must outlive its record, vectors, maps, streams and a yield.
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
