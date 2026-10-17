// Actual LLD/LLVM file owners; observations only, all expectations are in WAST.
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Filesystem.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Object/SymbolicFile.h"
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace llvm;
namespace fs = llvm::sys::fs;
static constexpr const char *Directory = "/tmp/lld-files";
static constexpr const char *Output = "/tmp/lld-files/out.wasm";
static void record(int64_t value) { write(1, &value, sizeof(value)); }
static void save(const char *path, StringRef data) {
  int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
  record(fd >= 0);
  record(fd < 0 ? -1 : write(fd, data.data(), data.size()));
  record(fd < 0 ? -1 : close(fd));
}
static void snapshot(const char *source, const char *destination) {
  auto buffer = MemoryBuffer::getFile(source);
  record(bool(buffer));
  if (buffer) save(destination, (*buffer)->getBuffer());
  else { record(0); record(-1); record(-1); }
}
static void contents(int fd, const char *destination) {
  char bytes[64] = {};
  record(lseek(fd, 0, SEEK_SET));
  ssize_t count = read(fd, bytes, sizeof(bytes));
  record(count);
  save(destination, StringRef(bytes, count > 0 ? count : 0));
}
static void entries() {
  std::error_code ec;
  unsigned count = 0;
  for (fs::directory_iterator it(Directory, ec), end; !ec && it != end;
       it.increment(ec)) ++count;
  record(ec.value()); record(count);
}
static void metadata(int fd) {
  struct stat s = {};
  record(fstat(fd, &s));
  record(s.st_ino); record(s.st_nlink); record(s.st_size); record(s.st_mode & 0777);
}
extern "C" int waste_c_pointer_formats(char *, size_t);
static void error(Error e) { record(errorToErrorCode(std::move(e)).value()); }

extern "C" int main(int, char **, char **) {
  record(LLVM_WASTE_GUEST); record(LLVM_ENABLE_THREADS);
  SmallString<0> diagnostics, messages;
  raw_svector_ostream stderrOS(diagnostics), stdoutOS(messages);
  auto *ctx = new lld::CommonLinkerContext;
  ctx->e.initialize(stdoutOS, stderrOS, false, false);
  ctx->e.logName = "wasm-ld";
  record(mkdir(Directory, 0700));
  save(Output, "old-output\n");
  int held = open(Output, O_RDONLY);
  record(held >= 0); metadata(held); entries();

  // The disabled-thread optimization leaves existing output to normal commit.
  for (unsigned requested : {1u, 0u, 8u}) {
    parallel::strategy.ThreadsRequested = requested;
    lld::unlinkAsync(Output);
    record(parallel::strategy.ThreadsRequested);
    record(access(Output, F_OK));
  }
  lld::unlinkAsync(Directory); lld::unlinkAsync("/tmp/lld-files/missing");
  snapshot(Output, "/tmp/lld-before.txt"); entries();

  // Real preflight cannot mutate the target or leave temporary files.
  record(lld::tryCreateFile("").value());
  record(lld::tryCreateFile("-").value());
  record(lld::tryCreateFile(Output).value());
  record(lld::tryCreateFile("/tmp/lld-files/absent.wasm").value());
  record(access("/tmp/lld-files/absent.wasm", F_OK));
  record(lld::tryCreateFile(Directory).value());
  record(lld::tryCreateFile("/tmp/lld-no-parent/out.wasm").value());
  snapshot(Output, "/tmp/lld-preflight.txt"); entries(); metadata(held);

  // Abandoning a real mapped temporary buffer retains the old file and fd.
  {
    auto buffer = FileOutputBuffer::create(Output, 16);
    record(bool(buffer));
    if (buffer) {
      record((*buffer)->getBufferSize());
      memset((*buffer)->getBufferStart(), 0xa5, 16);
      entries();
    } else { consumeError(buffer.takeError()); record(-1); entries(); }
  }
  entries(); snapshot(Output, "/tmp/lld-abandoned.txt");

  // Commit after a real shared sleep. Old inode/descriptor bytes remain live.
  {
    auto buffer = FileOutputBuffer::create(Output, 16, FileOutputBuffer::F_executable);
    record(bool(buffer));
    if (buffer) {
      record((*buffer)->getBufferSize());
      memcpy((*buffer)->getBufferStart(), "new\0wasm-output\n", 16);
      entries(); snapshot(Output, "/tmp/lld-pending.txt");
      const char marker[] = "__LLD_FILE_COMMIT_SLEEP__\n";
      write(2, marker, sizeof(marker) - 1);
      record(usleep(1000));
      record((*buffer)->getBufferStart()[0]);
      error((*buffer)->commit());
    } else { consumeError(buffer.takeError()); record(-1); }
  }
  entries(); snapshot(Output, "/tmp/lld-committed.txt");
  contents(held, "/tmp/lld-held.txt"); metadata(held);
  int current = open(Output, O_RDONLY);
  record(current >= 0); metadata(current); record(close(current)); record(close(held));

  // Explicit discard removes only the temporary file and keeps mapping memory.
  {
    auto buffer = FileOutputBuffer::create(Output, 7);
    record(bool(buffer));
    if (buffer) {
      memcpy((*buffer)->getBufferStart(), "discard", 7);
      entries(); (*buffer)->discard(); entries();
      record((*buffer)->getBufferStart()[0]);
      record((*buffer)->getBufferStart()[6]);
      (*buffer)->discard(); entries();
    } else consumeError(buffer.takeError());
  }
  entries(); snapshot(Output, "/tmp/lld-discarded.txt");

  // Force the upstream memory writer, and exercise its zero-sized output path.
  {
    auto buffer = FileOutputBuffer::create("/tmp/lld-files/in-memory", 6,
                                          FileOutputBuffer::F_mmap);
    record(bool(buffer));
    if (buffer) {
      record((*buffer)->getBufferSize()); entries();
      memcpy((*buffer)->getBufferStart(), "MEM\0!\n", 6);
      error((*buffer)->commit());
    } else consumeError(buffer.takeError());
  }
  snapshot("/tmp/lld-files/in-memory", "/tmp/lld-memory.txt"); entries();
  {
    auto buffer = FileOutputBuffer::create("/tmp/lld-files/empty", 0);
    record(bool(buffer));
    if (buffer) { record((*buffer)->getBufferSize()); error((*buffer)->commit()); }
    else consumeError(buffer.takeError());
  }
  snapshot("/tmp/lld-files/empty", "/tmp/lld-empty.txt"); entries();

  // Actual LLD ordinary writer truncates, buffers and supports positional writes.
  save("/tmp/lld-files/ordinary", "stale tail");
  {
    auto stream = lld::openFile("/tmp/lld-files/ordinary");
    record(bool(stream));
    if (stream) {
      *stream << "abcdef"; record(stream->tell());
      stream->pwrite("XY", 2, 2); record(stream->tell());
      *stream << "!\n"; record(stream->tell());
      stream->flush(); record(stream->has_error());
    }
  }
  snapshot("/tmp/lld-files/ordinary", "/tmp/lld-ordinary.txt");

  // LTO's regular stream uses actual read/seek/write and preserved offset.
  {
    auto stream = lld::openLTOOutputFile("/tmp/lld-files/lto");
    record(bool(stream));
    if (stream) {
      record(isa<raw_fd_stream>(stream.get()));
      *stream << "bitcode"; stream->flush(); record(stream->seek(0));
      char bytes[8] = {};
      if (auto *readable = dyn_cast<raw_fd_stream>(stream.get())) {
        ssize_t count = readable->read(bytes, 3); record(count);
        save("/tmp/lld-lto-prefix.txt", StringRef(bytes, count > 0 ? count : 0));
      } else record(-1);
      record(stream->tell()); *stream << "X";
      stream->pwrite("BC", 2, 0); record(stream->tell());
      stream->seek(7); *stream << "!\n";
      stream->flush(); record(stream->has_error());
    }
  }
  snapshot("/tmp/lld-files/lto", "/tmp/lld-lto.txt");

  // A nonregular output selects LTO's write-only fallback, keeping /dev/null.
  {
    auto stream = lld::openLTOOutputFile("/dev/null");
    record(bool(stream));
    if (stream) {
      record(isa<raw_fd_stream>(stream.get()));
      *stream << "discarded"; stream->flush(); record(stream->has_error());
    }
  }
  struct stat device = {}; record(stat("/dev/null", &device)); record(S_ISCHR(device.st_mode));
  {
    auto stream = lld::openFile("/tmp/lld-no-parent/ordinary"); record(bool(stream));
    auto lto = lld::openLTOOutputFile("/tmp/lld-no-parent/lto"); record(bool(lto));
  }
  record(ctx->e.errorCount); save("/tmp/lld-file-errors.txt", diagnostics);
  entries();
  lld::CommonLinkerContext::destroy(); record(lld::hasContext());
  // Public C and C++ pointer format macros, plus LLVM's actual DataRef formatter.
  record(sizeof(intptr_t)); record(sizeof(uintptr_t));
  char integerText[256] = {};
  record(waste_c_pointer_formats(integerText, sizeof(integerText)));
  save("/tmp/lld-pointer-c.txt", integerText);
  record(std::snprintf(integerText, sizeof(integerText),
                      "%" PRIdPTR " %" PRIiPTR " %" PRIuPTR " %" PRIoPTR
                      " %" PRIxPTR " %" PRIXPTR " %lld %u\n",
                      INTPTR_MIN, intptr_t(-17), UINTPTR_MAX,
                      uintptr_t(0xfedcba98), uintptr_t(0xfedcba98),
                      uintptr_t(0xfedcba98), -4294967298LL, 73u));
  save("/tmp/lld-pointer-cxx.txt", integerText);
  llvm::object::DataRefImpl ref;
  ref.p = uintptr_t(0xfedcba98);
  SmallString<0> objectText;
  raw_svector_ostream objectOS(objectText);
  objectOS << ref;
  save("/tmp/lld-pointer-llvm.txt", objectText);
  return 27;
}
