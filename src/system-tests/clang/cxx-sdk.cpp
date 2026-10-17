// Compile against generated libc++ headers and the actual mounted guest SDK.
// This is a declaration/ABI boundary probe, not a runnable Clang substitute.
#include <uchar.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <cinttypes>
#include <type_traits>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cfenv>
#include <cmath>
#include <ctime>
#include <chrono>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cwchar>
#include <cwctype>
#include <string_view>
#include <csetjmp>
#include <csignal>
#include <sysexits.h>
#include <sys/random.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <dirent.h>
#include <waste/process.h>

extern "C" DIR *opendir(const char *);
extern "C" DIR *fdopendir(int);
extern "C" int dirfd(DIR *);
extern "C" int closedir(DIR *);
extern "C" struct dirent *readdir(DIR *);
static_assert(std::is_same_v<decltype(&opendir), DIR *(*)(const char *)>);
static_assert(std::is_same_v<decltype(&readdir), struct dirent *(*)(DIR *)>);
int cxx_directory_probe(const char *path, int fd) {
  DIR *directory = opendir(path);
  readdir(directory);
  int descriptor = dirfd(directory);
  closedir(directory);
  return descriptor + closedir(fdopendir(fd));
}

extern "C" double modf(double, double *);
static_assert(std::is_same_v<decltype(std::modf(0.0, static_cast<double *>(nullptr))), double>);
static_assert(std::is_same_v<decltype(::log2(0.0)), double>);
static_assert(std::is_same_v<decltype(&::log2f), float (*)(float)>);
static_assert(std::is_same_v<decltype(std::log2(0.0f)), float>);
static_assert(std::is_same_v<decltype(::round(0.0)), double>);
float cxx_log2f_probe(float value) {
  return std::log2(value);
}
double cxx_modf_probe(double value, double *integer) {
  return std::modf(value, integer);
}

extern "C" ssize_t waste_get_executable_path(char *, size_t);
extern "C" int waste_get_exec_limits(struct waste_exec_limits *);
static_assert(std::is_same_v<decltype(&waste_get_exec_limits),
                           int (*)(struct waste_exec_limits *)>);
static_assert(sizeof(struct waste_exec_limits) == 3 * sizeof(uint32_t));
extern "C" int symlink(const char *, const char *);
extern "C" int symlinkat(const char *, int, const char *);
extern "C" int link(const char *, const char *);
extern "C" int linkat(int, const char *, int, const char *, int);
static_assert(std::is_same_v<decltype(&linkat),
                           int (*)(int, const char *, int, const char *, int)>);
static_assert(AT_SYMLINK_FOLLOW == 0x400);
static_assert(std::is_same_v<decltype(&symlinkat), int (*)(const char *, int, const char *)>);

extern "C" void *dlopen(const char *, int);
extern "C" void *dlsym(void *, const char *);
extern "C" int dlclose(void *);
extern "C" char *dlerror(void);
extern "C" void *mmap(void *, size_t, int, int, int, int64_t);
extern "C" int munmap(void *, size_t);
extern "C" int mprotect(void *, size_t, int);
extern "C" int msync(void *, size_t, int);
extern "C" int shm_open(const char *, int, mode_t);
extern "C" int shm_unlink(const char *);
static_assert(std::is_same_v<decltype(&mmap),
                           void *(*)(void *, size_t, int, int, int, int64_t)>);

extern "C" int kill(pid_t, int);
extern "C" int gethostname(char *, size_t);
extern "C" int waitpid(pid_t, int *, int);
extern "C" pid_t wait(int *);
extern "C" int execv(const char *, char *const []);
static_assert(std::is_same_v<decltype(&wait), pid_t (*)(int *)>);
static_assert(std::is_same_v<decltype(&execv), int (*)(const char *, char *const *)>);
static_assert(std::is_same_v<decltype(environ), char **>);

extern "C" int sigsetjmp(sigjmp_buf, int) __attribute__((returns_twice));
extern "C" [[noreturn]] void siglongjmp(sigjmp_buf, int);
extern "C" [[noreturn]] void longjmp(jmp_buf, int);
extern "C" sighandler_t signal(int, sighandler_t);
extern "C" int raise(int);
extern "C" ssize_t getrandom(void *, size_t, unsigned int);
static_assert(std::is_same_v<std::jmp_buf, jmp_buf>);
static_assert(std::is_same_v<decltype(&std::longjmp), void (*)(unsigned int *, int)>);
static_assert(EX_IOERR == 74 && EX_OK == 0 && EX_CONFIG == 78);

extern "C" size_t wcslen(const wchar_t *);
extern "C" int wmemcmp(const wchar_t *, const wchar_t *, size_t);
extern "C" wchar_t *wmemcpy(wchar_t *, const wchar_t *, size_t);
extern "C" wchar_t *wmemmove(wchar_t *, const wchar_t *, size_t);
extern "C" wchar_t *wmemset(wchar_t *, wchar_t, size_t);
extern "C" wint_t towlower(wint_t);
extern "C" int stat(const char *, struct stat *);
extern "C" int lstat(const char *, struct stat *);
extern "C" int fstat(int, struct stat *);
extern "C" int fstatat(int, const char *, struct stat *, int);
extern "C" int open(const char *, int, ...);
extern "C" int openat(int, const char *, int, ...);
extern "C" int fchmodat(int, const char *, unsigned int, int);
static_assert(sizeof(struct stat) == 128 && alignof(struct stat) == 8);
static_assert(std::is_same_v<decltype(((struct stat *)nullptr)->st_size), long long>);
static_assert(sizeof(wchar_t) == 4 && std::is_signed_v<wchar_t>);
static_assert(WCHAR_MIN == (-2147483647 - 1) && WCHAR_MAX == 2147483647);
static_assert(std::is_unsigned_v<wint_t> && sizeof(wint_t) == 4);
static_assert(WINT_MIN == 0 && WINT_MAX == UINT32_MAX);
static_assert(std::is_same_v<decltype(std::wmemcpy(nullptr, nullptr, 0)), wchar_t *>);
static_assert(std::is_same_v<decltype(std::towlower(0)), wint_t>);
size_t cxx_wide_probe(const std::wstring &text) {
  std::wstring_view view(text);
  return view.size() + view.find(L'\u00e9') + std::wcslen(text.c_str());
}

// The guest math providers must retain C linkage beneath libc++ wrappers.
extern "C" float sqrtf(float);
extern "C" double sqrt(double);
extern "C" long double frexpl(long double, int *);
static_assert(std::is_same_v<decltype(INFINITY), float>);
static_assert(std::is_same_v<decltype(NAN), float>);
static_assert(std::is_same_v<decltype(HUGE_VALF), float>);
static_assert(std::is_same_v<decltype(HUGE_VAL), double>);
static_assert(std::is_same_v<decltype(HUGE_VALL), long double>);
int cxx_classification_probe(long double value) {
  return std::fpclassify(value) + std::isfinite(value) + std::isnormal(value) +
         std::isnan(value) + std::isinf(value) + std::signbit(value);
}

// The public names must have C linkage and the wasm32 return widths.
extern "C" long strtol(const char *, char **, int);
extern "C" unsigned long strtoul(const char *, char **, int);
extern "C" long long strtoll(const char *, char **, int);
extern "C" unsigned long long strtoull(const char *, char **, int);
extern "C" intmax_t strtoimax(const char *, char **, int);
extern "C" uintmax_t strtoumax(const char *, char **, int);
extern "C" long wcstol(const wchar_t *, wchar_t **, int);
extern "C" unsigned long wcstoul(const wchar_t *, wchar_t **, int);
extern "C" long long wcstoll(const wchar_t *, wchar_t **, int);
extern "C" unsigned long long wcstoull(const wchar_t *, wchar_t **, int);
extern "C" intmax_t wcstoimax(const wchar_t *, wchar_t **, int);
extern "C" uintmax_t wcstoumax(const wchar_t *, wchar_t **, int);
extern "C" float wcstof(const wchar_t *, wchar_t **);
extern "C" double wcstod(const wchar_t *, wchar_t **);
extern "C" long double wcstold(const wchar_t *, wchar_t **);
static_assert(sizeof(long) == 4 && sizeof(long long) == 8);
static_assert(std::is_same_v<decltype(std::strtol("", nullptr, 10)), long>);
static_assert(std::is_same_v<decltype(std::strtoul("", nullptr, 10)), unsigned long>);
static_assert(std::is_same_v<decltype(std::strtoll("", nullptr, 10)), long long>);
static_assert(std::is_same_v<decltype(std::strtoull("", nullptr, 10)), unsigned long long>);
static_assert(std::is_same_v<decltype(std::strtoimax("", nullptr, 10)), intmax_t>);
static_assert(std::is_same_v<decltype(std::strtoumax("", nullptr, 10)), uintmax_t>);
static_assert(std::is_same_v<decltype(std::wcstol(L"", nullptr, 10)), long>);
static_assert(std::is_same_v<decltype(std::wcstoul(L"", nullptr, 10)), unsigned long>);
static_assert(std::is_same_v<decltype(std::wcstoll(L"", nullptr, 10)), long long>);
static_assert(std::is_same_v<decltype(std::wcstoull(L"", nullptr, 10)), unsigned long long>);
static_assert(std::is_same_v<decltype(std::wcstoimax(L"", nullptr, 10)), intmax_t>);
static_assert(std::is_same_v<decltype(std::wcstoumax(L"", nullptr, 10)), uintmax_t>);
static_assert(std::is_same_v<decltype(std::wcstof(L"", nullptr)), float>);
static_assert(std::is_same_v<decltype(std::wcstod(L"", nullptr)), double>);
static_assert(std::is_same_v<decltype(std::wcstold(L"", nullptr)), long double>);
uintmax_t cxx_integer_probe(const char *text, char **end) {
  return std::strtol(text, end, 0) + std::strtoul(text, end, 0) +
         std::strtoll(text, end, 0) + std::strtoull(text, end, 0) +
         std::strtoimax(text, end, 0) + std::strtoumax(text, end, 0);
}

extern "C" int vsnprintf(char *, size_t, const char *, va_list);
extern "C" int vsprintf(char *, const char *, va_list);
extern "C" int swprintf(wchar_t *, size_t, const wchar_t *, ...);
extern "C" int vswprintf(wchar_t *, size_t, const wchar_t *, va_list);
static_assert(std::is_same_v<decltype(std::swprintf(nullptr, 0, L"")), int>);
int cxx_wide_format_probe(wchar_t *output, size_t size, const wchar_t *format, va_list args) {
  return std::vswprintf(output, size, format, args);
}
int cxx_format_probe(char *output, size_t size, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = std::vsnprintf(output, size, format, args);
  va_end(args);
  return result;
}
int cxx_unbounded_format_probe(char *output, const char *format, va_list args) {
  return std::vsprintf(output, format, args);
}

extern "C" int clock_gettime(clockid_t, struct timespec *);
extern "C" int gettimeofday(struct timeval *, void *);
static_assert(sizeof(clockid_t) == 4 && sizeof(timespec) == 16);
static_assert(offsetof(timespec, tv_nsec) == 8);
static_assert(CLOCK_REALTIME == 0 && CLOCK_MONOTONIC == 1);
static_assert(std::chrono::steady_clock::is_steady);
#if defined(_POSIX_TIMERS) && _POSIX_TIMERS > 0
#error Unsupported POSIX timer suite advertised
#endif
auto cxx_clock_probe() {
  timespec value;
  timeval wall;
  clock_gettime(CLOCK_MONOTONIC, &value);
  gettimeofday(&wall, nullptr);
  return std::chrono::steady_clock::now() - std::chrono::steady_clock::now();
}

// Include unistd before cstdlib: their shared temporary-file declarations
// must retain C linkage, just as they do in libc++abi's guard implementation.
extern "C" int mkstemp(char *);
extern "C" int mkostemp(char *, int);
void cxx_abort_probe() { std::abort(); }
void *cxx_alignment_probe() { return std::aligned_alloc(4096, 8192); }
int cxx_fenv_probe() {
  std::fenv_t env;
  std::fexcept_t flags;
  return std::fegetenv(&env) || std::feholdexcept(&env) ||
         std::fegetexceptflag(&flags, FE_ALL_EXCEPT) ||
         std::fesetexceptflag(&flags, FE_ALL_EXCEPT) ||
         std::feclearexcept(FE_ALL_EXCEPT) || std::feraiseexcept(FE_ALL_EXCEPT) ||
         std::fetestexcept(FE_ALL_EXCEPT) || std::fesetround(FE_TONEAREST) ||
         std::fegetround() != FE_TONEAREST || std::fesetenv(FE_DFL_ENV) ||
         std::feupdateenv(&env);
}
static_assert(sizeof(std::fenv_t) == 4);
static_assert(sizeof(std::fexcept_t) == 4);
static_assert(FE_ALL_EXCEPT == 0 && FE_TONEAREST == 0);
#if defined(FE_UPWARD) || defined(FE_DOWNWARD) || defined(FE_TOWARDZERO) || \
    defined(FE_INEXACT) || defined(FE_INVALID) || defined(FE_DIVBYZERO) || \
    defined(FE_OVERFLOW) || defined(FE_UNDERFLOW)
#error Unsupported floating-point environment capability advertised
#endif

static_assert(sizeof(char16_t) == 2);
static_assert(sizeof(char32_t) == 4);
static_assert(sizeof(div_t) == 8);
static_assert(sizeof(ldiv_t) == 8);
static_assert(sizeof(lldiv_t) == 16);

int cxx_sdk_probe() {
  std::vector<std::string> words;
  words.emplace_back("clang");
  div_t small = std::div(-17, 5);
  ldiv_t medium = std::ldiv(17, -5);
  lldiv_t large = std::lldiv(-4294967297LL, 2);
  errno = 0;
  return words[0].size() == 5 && small.quot == -3 && small.rem == -2 &&
         medium.quot == -3 && medium.rem == 2 &&
         large.quot == -2147483648LL && large.rem == -1 &&
         std::llabs(-4294967297LL) == 4294967297LL;
}
