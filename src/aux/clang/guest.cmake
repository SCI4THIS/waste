# Options for the guest executables only; host generators keep their profile.
set(LLVM_ENABLE_LIBCXX ON CACHE BOOL "")
set(LLVM_HOST_TRIPLE wasm32-unknown-unknown CACHE STRING "")
set(LLVM_WASTE_GUEST ON CACHE BOOL "")
set(LLD_WASTE_WASM_ONLY ON CACHE BOOL "")

# Preserve PIC and the static LLVM LTO component used by LLD. These optional
# C API libraries/plugins require CMake SHARED/MODULE targets on Generic.
set(LLVM_TOOL_LTO_BUILD OFF CACHE BOOL "")
set(LLVM_TOOL_REMARKS_SHLIB_BUILD OFF CACHE BOOL "")
set(CLANG_TOOL_LIBCLANG_BUILD OFF CACHE BOOL "")
set(LLVM_ENABLE_PLUGINS OFF CACHE BOOL "")
set(CLANG_PLUGIN_SUPPORT OFF CACHE BOOL "")

# The guest has a shared libc provider, not separate native system libraries.
# Archive-only check_library_exists probes cannot establish their existence.
# These library flags say nothing about individual libc function availability;
# header diagnostics and the final shared-import audit still enforce that ABI.
set(HAVE_LIBPTHREAD OFF CACHE BOOL "")
set(HAVE_LIBDL OFF CACHE BOOL "")
set(HAVE_LIBRT OFF CACHE BOOL "")
set(HAVE_LIBPFM_INITIALIZE OFF CACHE BOOL "")

# Archive-only symbol/library probes do not resolve imports in this build. These
# frame/unwinder functions have no guest provider; pthread rwlocks are public
# unavailable declarations. Do not turn those archive successes into ABI claims.
set(HAVE_REGISTER_FRAME OFF CACHE BOOL "")
set(HAVE_DEREGISTER_FRAME OFF CACHE BOOL "")
set(HAVE_UNW_ADD_DYNAMIC_FDE OFF CACHE BOOL "")
set(HAVE_PTHREAD_RWLOCK_INIT OFF CACHE BOOL "")

# This archive-only Clang probe cannot resolve compiler-rt's emulated-TLS
# helper. Neither the guest libc nor the linked C++/Support archives provide
# it. In particular, do not force an absent runtime into Clang's optional JIT.
# The native generator profile retains its ordinary upstream probe.
set(CLANG_HAVE_EMUTLS_GET_ADDRESS OFF CACHE BOOL "")

# A declaration/import is not guest CPU accounting. The shared provider returns
# ENOSYS; select LLVM's existing no-accounting branch instead of reading usage.
set(HAVE_GETRUSAGE OFF CACHE BOOL "")

# Guest signals carry a number, not a native fault context or machine stack.
# Keep real dlopen support without selecting the dladdr backtrace fallback.
set(LLVM_ENABLE_BACKTRACES OFF CACHE BOOL "")
