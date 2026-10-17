// Include the complete production owner to observe its anonymous-namespace helper.
#include WASTE_CHECK_SOURCE
#include "observations.h"
#include <unistd.h>
extern "C" int main(int, char **) {
  const auto Bytes = spirvNameObservations();
  (void)::usleep(1000);
  (void)::write(1, Bytes.data(), Bytes.size());
  return 38;
}
