# All target includes come from the guest SDK; never fall back to host C headers.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR wasm32)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_COMPILER "$ENV{WASTE_CLANG_CC}")
set(CMAKE_CXX_COMPILER "$ENV{WASTE_CLANG_CXX}")
set(CMAKE_C_COMPILER_TARGET wasm32-unknown-unknown)
set(CMAKE_CXX_COMPILER_TARGET wasm32-unknown-unknown)
set(CMAKE_AR "$ENV{WASTE_CLANG_AR}")
set(CMAKE_RANLIB "$ENV{WASTE_CLANG_RANLIB}")
set(CMAKE_C_FLAGS_INIT "-ffreestanding -fno-builtin -fPIC -nostdinc -nostdlib -isystem $ENV{WASTE_SDK_ROOT}/usr/include -isystem $ENV{WASTE_SDK_ROOT}/usr/lib/waste/cc/include")
if(WASTE_CLANG_PRIVATE_INCLUDE)
  # Private adapters must precede the C SDK they forward to with include_next.
  # libc++ wrappers are prepended below, preserving their own include order.
  string(PREPEND CMAKE_C_FLAGS_INIT "-isystem ${WASTE_CLANG_PRIVATE_INCLUDE} ")
endif()
# Select RTTI per target: libc++abi's typeinfo implementation requires it,
# while LIBCXX_ENABLE_RTTI/LLVM_ENABLE_RTTI keep the clients without RTTI.
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -fno-exceptions")
string(APPEND CMAKE_CXX_FLAGS_INIT " -isystem $ENV{WASTE_CLANG_SOURCE}/libunwind/include")
if(DEFINED ENV{WASTE_CLANG_CXX_HEADERS})
  # libc++ uses include_next to reach the guest C headers. Its wrappers must
  # precede those headers, including for LLVM's configure-time probes.
  string(PREPEND CMAKE_CXX_FLAGS_INIT "-isystem $ENV{WASTE_CLANG_CXX_HEADERS} ")
endif()
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--no-entry,--import-memory,--import-table,--experimental-pic,--unresolved-symbols=import-dynamic,--export=_start,--export=main,--export=__heap_base,--export=__data_end,--export=__stack_pointer")
set(CMAKE_FIND_ROOT_PATH "$ENV{WASTE_SDK_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
