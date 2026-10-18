// Record actual LLVM buffering/formatting through a nonterminal memory sink.
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <unistd.h>

static int32_t Values[64];
static unsigned Count;
static char Bytes[4][256];
static void record(int32_t Value) {
  if (Count == 63) _exit(95);
  Values[Count++] = Value;
}
static void capture(unsigned Slot, const std::string &Text) {
  if (Text.size() > sizeof(Bytes[Slot])) _exit(96);
  record(Text.size());
  std::memcpy(Bytes[Slot], Text.data(), Text.size());
}

// This sink only captures output. LLVM owns buffering, formatting, tell and
// pwrite dispatch. File-descriptor providers are separate.
class MemoryStream final : public llvm::raw_pwrite_stream {
  std::string &Text;
  void write_impl(const char *Data, size_t Size) override {
    Text.append(Data, Size);
    ++Writes;
  }
  void pwrite_impl(const char *Data, size_t Size, uint64_t Offset) override {
    Text.replace(Offset, Size, Data, Size);
    ++PWrites;
  }
  uint64_t current_pos() const override { return Text.size(); }
  size_t preferred_buffer_size() const override { return 8; }
public:
  unsigned Writes = 0, PWrites = 0;
  MemoryStream(std::string &Text, bool Unbuffered)
      : raw_pwrite_stream(Unbuffered), Text(Text) {}
  ~MemoryStream() override { flush(); }
};

extern "C" int main(int argc, char **argv, char **environment) {
  (void)argc; (void)argv; (void)environment;
  record(LLVM_WASTE_GUEST);
  record(_LIBCPP_HAS_LOCALIZATION);
  record(LLVM_ENABLE_THREADS);
  std::string Buffered;
  {
    MemoryStream OS(Buffered, false);
    OS.SetBufferSize(8);
    record(OS.GetBufferSize());
    OS.write("abc", 3);
    record(OS.tell()); record(Buffered.size());
    record(OS.GetNumBytesInBuffer()); record(OS.Writes);
    OS << ':';
    OS.flush();
    record(OS.tell()); record(Buffered.size());
    record(OS.GetNumBytesInBuffer()); record(OS.Writes);
    OS.write("0123456789abcdefG", 17);
    record(OS.tell()); record(Buffered.size());
    record(OS.GetNumBytesInBuffer()); record(OS.Writes);
    OS.SetBufferSize(3);
    record(OS.tell()); record(Buffered.size());
    record(OS.GetNumBytesInBuffer()); record(OS.Writes);
    record(OS.GetBufferSize());
    OS.write("XYZ", 3);
    record(OS.tell()); record(Buffered.size()); record(OS.GetNumBytesInBuffer());
    OS.pwrite("AB", 2, 0);
    record(OS.tell()); record(Buffered.size()); record(OS.GetNumBytesInBuffer());
    record(OS.Writes); record(OS.PWrites);
    OS.SetUnbuffered();
    OS.write_zeros(3);
    OS << '!';
    record(OS.tell()); record(OS.GetNumBytesInBuffer()); record(OS.Writes);
  }
  capture(0, Buffered);

  std::string Unbuffered;
  {
    llvm::raw_string_ostream OS(Unbuffered);
    const char Binary[] = "binary:\0\xff\n";
    OS.write(Binary, sizeof(Binary) - 1);
    OS << '|' << UINT64_MAX << '/' << INT64_MIN << '/';
    OS.write_hex(UINT64_C(0xfedcba9876543210));
    OS.indent(3);
    const char Escapes[] = "\t\n\"\\\x01\xff";
    OS.write_escaped(llvm::StringRef(Escapes, sizeof(Escapes) - 1), true);
    record(OS.tell()); record(OS.GetNumBytesInBuffer());
  }
  std::string Copy(Unbuffered);
  Unbuffered[0] = 'Q';
  record(Unbuffered[0]); record(Copy[0]);
  capture(1, Unbuffered); capture(2, Copy);

  std::string Input(193, 'r'), Flushed;
  Input[0] = 'a'; Input[96] = '@'; Input[192] = 'z';
  {
    MemoryStream OS(Flushed, false);
    OS.SetBufferSize(7);
    OS.write(Input.data(), Input.size());
    record(OS.tell()); record(Flushed.size()); record(OS.GetNumBytesInBuffer());
    Input.assign(193, 'X');
    record(Flushed.front());
    // The destructor must flush the remaining copied bytes.
  }
  capture(3, Flushed);
  Values[63] = Count;
  if (write(1, Values, sizeof(Values)) != (ssize_t)sizeof(Values)) return 1;
  return write(1, Bytes, sizeof(Bytes)) == (ssize_t)sizeof(Bytes) ? 0 : 1;
}
