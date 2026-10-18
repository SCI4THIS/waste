cmake_minimum_required(VERSION 3.20)
project(WasteCoffLinker LANGUAGES CXX)

# Use the configured guest LLVM dependency graph without reconfiguring it.
# All paths are supplied by the aux wrapper; no host LLVM package fallback.
foreach(input LLVM_SOURCE LLVM_BUILD COFF_SOURCE LLVM_TABLEGEN)
  if(NOT DEFINED ${input} OR NOT EXISTS "${${input}}")
    message(FATAL_ERROR "${input} must name an existing private build input")
  endif()
endforeach()
find_package(LLVM REQUIRED CONFIG PATHS "${LLVM_BUILD}/lib/cmake/llvm"
             NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
if(NOT CMAKE_CXX_COMPILER_TARGET STREQUAL "wasm32-unknown-unknown")
  message(FATAL_ERROR "lld-link requires the guest Wasm toolchain")
endif()
file(STRINGS "${LLVM_BUILD}/include/llvm/Config/llvm-config.h" guest_mode
     REGEX "^#define LLVM_WASTE_GUEST 1$")
file(STRINGS "${LLVM_BUILD}/include/llvm/Config/llvm-config.h" guest_threads
     REGEX "^#define LLVM_ENABLE_THREADS 0$")
if(NOT guest_mode OR NOT guest_threads)
  message(FATAL_ERROR "lld-link requires the single-thread guest LLVM configuration")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(LLD_WASTE_NO_LTO=1 _FILE_OFFSET_BITS=64
  _LARGEFILE_SOURCE __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS
  __STDC_LIMIT_MACROS)
add_compile_options(-fno-rtti -fno-exceptions -ffunction-sections
                    -fdata-sections -fvisibility-inlines-hidden)
include_directories("${CMAKE_CURRENT_BINARY_DIR}" "${COFF_SOURCE}"
  "${LLVM_SOURCE}/lld/include" "${LLVM_BUILD}/tools/lld/include"
  "${LLVM_BUILD}/include" "${LLVM_SOURCE}/llvm/include")

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Options.inc"
  COMMAND "${LLVM_TABLEGEN}" -gen-opt-parser-defs
          -I "${LLVM_SOURCE}/llvm/include" -I "${COFF_SOURCE}"
          "${COFF_SOURCE}/Options.td" -o "${CMAKE_CURRENT_BINARY_DIR}/Options.inc"
  DEPENDS "${COFF_SOURCE}/Options.td"
          "${LLVM_SOURCE}/llvm/include/llvm/Option/OptParser.td"
          "${LLVM_TABLEGEN}"
  VERBATIM)

# Keep this selection explicit: in particular, LTO.cpp and target codegen
# archives must not enter the linker just because Clang enables X86.
set(coff_sources CallGraphSort Chunks COFFLinkerContext DebugTypes DLL Driver
  DriverUtils ICF InputFiles LLDMapFile MapFile MarkLive MinGW PDB SymbolTable
  Symbols Writer)
list(TRANSFORM coff_sources PREPEND "${COFF_SOURCE}/")
list(TRANSFORM coff_sources APPEND ".cpp")
add_library(wasteCOFF STATIC ${coff_sources} Options.inc)

set(common_sources Args CommonLinkerContext DriverDispatcher DWARF ErrorHandler
  Filesystem Memory Reproduce Strings TargetOptionsCommandFlags Timer Utils Version)
list(TRANSFORM common_sources PREPEND "${LLVM_SOURCE}/lld/Common/")
list(TRANSFORM common_sources APPEND ".cpp")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/VCSVersion.inc"
  COMMAND "${CMAKE_COMMAND}" -DNAMES=LLD
    "-DHEADER_FILE=${CMAKE_CURRENT_BINARY_DIR}/VCSVersion.inc"
    "-DLLVM_FORCE_VC_REVISION=${LLVM_FORCE_VC_REVISION}"
    "-DLLVM_FORCE_VC_REPOSITORY=${LLVM_FORCE_VC_REPOSITORY}"
    -P "${LLVM_SOURCE}/llvm/cmake/modules/GenerateVersionFromVCS.cmake"
  DEPENDS "${LLVM_SOURCE}/llvm/cmake/modules/GenerateVersionFromVCS.cmake"
  VERBATIM)
add_library(wasteLldCommon STATIC ${common_sources} VCSVersion.inc)
target_link_libraries(wasteLldCommon PUBLIC LLVMCodeGen LLVMCore
  LLVMDebugInfoDWARF LLVMDemangle LLVMMC LLVMOption LLVMSupport LLVMTarget
  LLVMTargetParser)
target_link_libraries(wasteCOFF PUBLIC wasteLldCommon LLVMBinaryFormat
  LLVMBitWriter LLVMCore LLVMDebugInfoCodeView LLVMDebugInfoDWARF
  LLVMDebugInfoMSF LLVMDebugInfoPDB LLVMDemangle LLVMMC
  LLVMObject LLVMOption LLVMSupport LLVMTargetParser LLVMWindowsDriver
  LLVMWindowsManifest)

add_executable(lld-link main.cpp)
target_link_libraries(lld-link PRIVATE wasteCOFF)
target_link_options(lld-link PRIVATE
  "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/lld-link.map")
