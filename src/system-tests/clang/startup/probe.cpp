// Production CRT and current libc++ observations; WAST owns expectations.
#include <fcntl.h>
#include <new>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <type_traits>
#include <unistd.h>
#include <vector>

extern "C" FILE *waste_stdin() __attribute__((import_module("libc")));
extern "C" FILE *waste_stdout() __attribute__((import_module("libc")));
extern "C" FILE *waste_stderr() __attribute__((import_module("libc")));

static uint32_t Values[128], Count, Sequence, Alive, Destroyed;
static void add(uint32_t value) { if (Count < 128) Values[Count++] = value; }
struct Local {
  Local() { ++Alive; }
  ~Local() { --Alive; ++Destroyed; }
};
struct alignas(64) Aligned { uint32_t Value; };
static Aligned *Owned;
static char *Text;

__attribute__((constructor(201))) static void early() {
  Sequence = Sequence * 10 + 1;
  add(Sequence);                                             // 0
  add(stdin != nullptr); add(stdout != nullptr); add(stderr != nullptr);
  add(stdin && stdin == waste_stdin());                      // 4
  add(stdout && stdout == waste_stdout());
  add(stderr && stderr == waste_stderr());
  add(stdin ? fileno(stdin) : -1);                           // 7
  add(stdout ? fileno(stdout) : -1);
  add(stderr ? fileno(stderr) : -1);
  const char *environment = getenv("WASTE_STARTUP");
  add(environment != nullptr);                              // 10
  add(environment && !strcmp(environment, "constructor-env"));
  add(Alive); add(Destroyed);                                // 12
  {
    Local local;
    add(Alive); add(Destroyed);                              // 14
    std::string original(80, 's');
    original += ":ctor";
    std::string copied = original;
    original[0] = 'x';
    add(original.size()); add(copied.size());                // 16
    add(original[0]); add(copied[0]);
    add(original.data() != copied.data());
    add(copied.c_str()[copied.size()]);
    Text = new char[copied.size() + 1];
    memcpy(Text, copied.c_str(), copied.size() + 1);
    std::vector<uint32_t> numbers;
    for (uint32_t i = 0; i < 17; ++i) numbers.push_back(i * i);
    uint32_t sum = 0;
    for (auto value : numbers) sum += value;
    add(numbers.size()); add(sum);                           // 22
    add(numbers.capacity() >= numbers.size());
  }
  add(Alive); add(Destroyed);                                // 25
  Owned = new Aligned[3];
  add(reinterpret_cast<uintptr_t>(Owned) % alignof(Aligned)); // 27
  add(reinterpret_cast<uintptr_t>(Owned + 1) % alignof(Aligned));
  add(reinterpret_cast<char *>(Owned + 1) - reinterpret_cast<char *>(Owned));
  for (uint32_t i = 0; i < 3; ++i) Owned[i].Value = 100 + i;
  void *memory = ::operator new(513, std::align_val_t(256));
  add(reinterpret_cast<uintptr_t>(memory) % 256);             // 30
  memset(memory, 0xa5, 513);
  add(static_cast<unsigned char *>(memory)[0]);
  add(static_cast<unsigned char *>(memory)[512]);
  ::operator delete(memory, std::align_val_t(256));
  char input[64] = {};
  add(stdin && fgets(input, sizeof(input), stdin) == input); // 33
  add(!strcmp(input, "constructor-input\n"));
  // This uses the existing sleep continuation, including when called in ctors.
  add(usleep(1000));                                         // 35
  add(Sequence);
  add(stdin && stdin == waste_stdin());
  add(stdout && stdout == waste_stdout());
  add(stderr && stderr == waste_stderr());
}

__attribute__((constructor(301))) static void later() {
  Sequence = Sequence * 10 + 2;
  add(Sequence);                                             // 40
  add(stdout ? fprintf(stdout, "ctor-out:%u\n", Sequence) : -1);
  add(stderr ? fprintf(stderr, "ctor-err:%u\n", Sequence) : -1);
}

struct Startup {
  Startup() {
    Sequence = Sequence * 10 + 3;
    add(Sequence);                                           // 43
    add(strlen(Text)); add(Text[0]); add(Text[84]); add(Text[85]);
    add(Owned[0].Value); add(Owned[1].Value); add(Owned[2].Value);
  }
};
static_assert(std::is_trivially_destructible_v<Startup>);
static Startup Initialized;

#ifdef WASTE_MAIN_TWO_ARGS
extern "C" int main(int argc, char **argv) {
#else
extern "C" int main(int argc, char **argv, char **env) {
#endif
  add(Sequence);                                             // 51
  add(argc);
  add(argc > 1 && !strcmp(argv[1], "alpha"));
  add(argc > 2 && !strcmp(argv[2], "beta"));
  add(argv[argc] == nullptr);
#ifdef WASTE_MAIN_TWO_ARGS
  add(getenv("WASTE_STARTUP") != nullptr);
#else
  add(env != nullptr);
#endif
  add(getenv("WASTE_STARTUP") &&
      !strcmp(getenv("WASTE_STARTUP"), "constructor-env"));
  add(Alive); add(Destroyed);                                // 58
  add(stdin == waste_stdin()); add(stdout == waste_stdout()); add(stderr == waste_stderr());
  add(Owned[0].Value); add(Owned[1].Value); add(Owned[2].Value); // 63
  add(strlen(Text)); add(Text[0]); add(Text[80]); add(Text[85]);
  char input[64] = {};
  add(fgets(input, sizeof(input), stdin) == input);            // 70
  add(!strcmp(input, "main-input\n"));
  add(fgetc(stdin)); add(feof(stdin));
  add(fprintf(stdout, "main-out:%u\n", Sequence));            // 74
  add(fprintf(stderr, "main-err:%u\n", Sequence));
  // Current descriptor streams write directly; this does not test buffering.
  add(ferror(stdout));
  add(fputs("return-write\n", stdout) < 0);
  delete[] Text; delete[] Owned;
  {
    Local local;
    add(Alive); add(Destroyed);                              // 78
  }
  add(Alive); add(Destroyed);                                // 80
  int fd = open("/tmp/startup.bin", O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd >= 0) { write(fd, Values, Count * sizeof(Values[0])); close(fd); }
  return 23;
}
