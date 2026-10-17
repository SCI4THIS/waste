# LLVM-specific header spellings stay private to the guest compiler build.
set(WASTE_CLANG_PRIVATE_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/include")
include("${CMAKE_CURRENT_LIST_DIR}/toolchain.cmake")
