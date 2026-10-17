#ifndef WASTE_SPIRV_NAME_CASES_H
#define WASTE_SPIRV_NAME_CASES_H
#include <string>
#include <vector>
inline std::vector<std::string> spirvNameInputs() {
  return {"dxc", "DXC", "khr", "KHR", "SPV_a", "SPV_Z", "SPV_0", "SPV__",
          "SPV_KHR_shader_clock", "SPV_" + std::string(1025, 'A') + "_9",
          "", "Dxc", "dxC", "SPV_", "spv_KHR_test", "xSPV_KHR_test",
          "SPV_a-b", "SPV_a+b", "SPV_a,b", "SPV_a b", "SPV_a\n", "dxc\n",
          "dxc\r\n", "DXC ", std::string("SPV_a\0tail", 10),
          std::string("dxc\0", 4), "SPV_\xc3\xa9", "SPV_\t", "KHRjunk", "SPV_a\r"};
}
#endif
