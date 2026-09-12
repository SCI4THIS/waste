/* stdio.c — Wasm stdio implementation.
 *
 * WASTE_ENGINE build: minimal stubs to satisfy linker for the engine binary.
 * Guest libc build:   full FILE management, I/O operations, and printf family.
 */

#ifdef WASTE_ENGINE
/* ---- Engine build: no-op stubs ---- */
#include <stdio.h>

FILE __stdin_file  = { .fd = 0, .error = 0, .eof = 0 };
FILE __stdout_file = { .fd = 1, .error = 0, .eof = 0 };
FILE __stderr_file = { .fd = 2, .error = 0, .eof = 0 };
int fprintf(FILE *f, const char *fmt, ...) { (void)f; (void)fmt; return 0; }
size_t fwrite(const void *p, size_t sz, size_t n, FILE *f) { (void)p; (void)sz; (void)n; (void)f; return 0; }
size_t fread(void *p, size_t sz, size_t n, FILE *f) { (void)p; (void)sz; (void)n; (void)f; return 0; }
int fputc(int c, FILE *f) { (void)c; (void)f; return 0; }
int fputs(const char *s, FILE *f) { (void)s; (void)f; return 0; }
int ferror(FILE *f) { (void)f; return 0; }
FILE *fopen(const char *path, const char *mode) { (void)path; (void)mode; return (void *)0; }
int fseek(FILE *f, long off, int whence) { (void)f; (void)off; (void)whence; return 0; }
long ftell(FILE *f) { (void)f; return 0; }
int fclose(FILE *f) { (void)f; return 0; }
int putchar(int c) { (void)c; return 0; }
int printf(const char *fmt, ...) { (void)fmt; return 0; }
int getc(FILE *f) { (void)f; return -1; }
void clearerr(FILE *f) { (void)f; }
int fileno(FILE *f) { (void)f; return -1; }
void perror(const char *s) { (void)s; }
char *strerror(int n) { (void)n; return "error"; }

#else
/* ---- Guest libc: full FILE I/O and printf ---- */
#include "common.h"

#ifdef WASTE_POSIX_IO
extern i32 open(const char *path, i32 flags, i32 mode);
extern i32 close(i32 descriptor);
extern i32 read(i32 descriptor, void *buffer, u32 count);
extern i32 write(i32 descriptor, const void *buffer, u32 count);
#endif

enum {
  FILE_MAGIC = 0x5746494c,
  FILE_READ = 1,
  FILE_WRITE = 2,
  FILE_APPEND = 4,
  FILE_OWN_BUFFER = 8,
  FILE_CLOSED = 16,
  EOF_VALUE = -1
};

typedef struct WasteFile {
  u32 magic;
  i32 descriptor;
  u32 flags;
  i32 error;
  i32 end_of_file;
  unsigned char *data;
  u32 capacity;
  u32 length;
  u32 position;
} FILE;

static FILE *standard_input;
static FILE *standard_output;
static FILE *standard_error;

static FILE *file_create(unsigned char *buffer, u32 capacity, u32 length,
                         u32 flags, i32 descriptor) {
  FILE *file = malloc((u32)sizeof(FILE));
  if (!file) return 0;
  file->magic = FILE_MAGIC;
  file->descriptor = descriptor;
  file->flags = flags;
  file->error = 0;
  file->end_of_file = 0;
  file->data = buffer;
  file->capacity = capacity;
  file->length = length <= capacity ? length : capacity;
  file->position = (flags & FILE_APPEND) ? file->length : 0;
  return file;
}

i32 waste_stdio_init(u32 capacity) {
  if (capacity < 64) capacity = 64;
  unsigned char *in = malloc(capacity);
  unsigned char *out = malloc(capacity);
  unsigned char *err = malloc(capacity);
  if (!in || !out || !err) return 0;
  standard_input = file_create(in, capacity, 0, FILE_READ | FILE_OWN_BUFFER, 0);
  standard_output = file_create(out, capacity, 0,
      FILE_WRITE | FILE_APPEND | FILE_OWN_BUFFER, 1);
  standard_error = file_create(err, capacity, 0,
      FILE_WRITE | FILE_APPEND | FILE_OWN_BUFFER, 2);
  return standard_input && standard_output && standard_error;
}

/* A separately linked guest may retain C runtime pointer slots for stdin,
   stdout, and stderr in the shared address space. Populate those slots with
   this libc instance's handles after waste_stdio_init(). */
i32 waste_stdio_bind(u32 input_slot, u32 output_slot, u32 error_slot) {
  u32 memory_size = __builtin_wasm_memory_size(0) * 65536U;
  if (!standard_input || !standard_output || !standard_error ||
      memory_size < 4 || input_slot > memory_size - 4 ||
      output_slot > memory_size - 4 || error_slot > memory_size - 4)
    return 0;
  *(FILE **)(unsigned long)input_slot = standard_input;
  *(FILE **)(unsigned long)output_slot = standard_output;
  *(FILE **)(unsigned long)error_slot = standard_error;
  return 1;
}

FILE *waste_stdin(void) { return standard_input; }
FILE *waste_stdout(void) { return standard_output; }
FILE *waste_stderr(void) { return standard_error; }
unsigned char *waste_file_data(FILE *file) { return file ? file->data : 0; }
u32 waste_file_length(FILE *file) { return file ? file->length : 0; }

FILE *waste_fmemopen(void *buffer, u32 capacity, u32 length, u32 flags) {
  return file_create(buffer, capacity, length, flags, -1);
}

FILE *fdopen(i32 descriptor, const char *mode) {
  if (descriptor == 0 && standard_input) return standard_input;
  if (descriptor == 1 && standard_output) return standard_output;
  if (descriptor == 2 && standard_error) return standard_error;
  u32 flags = mode && mode[0] == 'r' ? FILE_READ : FILE_WRITE;
  if (mode && mode[0] == 'a') flags |= FILE_APPEND;
  unsigned char *buffer = malloc(4096);
  if (!buffer) return 0;
  return file_create(buffer, 4096, 0, flags | FILE_OWN_BUFFER, descriptor);
}

FILE *fopen(const char *path, const char *mode) {
#ifdef WASTE_POSIX_IO
  i32 flags = mode && mode[0] == 'r' ? 0 : 65;
  if (mode && mode[0] == 'a') flags |= 1024;
  if (mode && mode[0] == 'w') flags |= 512;
  i32 descriptor = open(path, flags, 0666);
  return descriptor < 0 ? 0 : fdopen(descriptor, mode);
#else
  (void)path; (void)mode;
  *__errno_location() = 38; /* ENOSYS until the VFS syscall ABI is linked. */
  return 0;
#endif
}

u32 fwrite(const void *pointer, u32 size, u32 members, FILE *file) {
  if (!file || !size) {
    return 0;
  }
  u64 wanted64 = (u64)size * members;
  if (wanted64 > 0xffffffffULL) return 0;
  u32 wanted = (u32)wanted64;
#ifdef WASTE_POSIX_IO
  /* bash.wat was compiled against an opaque FILE layout that is not present
     in the imported libc module. Its non-null stdout/stderr handles are still
     valid stream tokens; route those writes through the process stdout until
     the original libc's FILE layout can be identified. */
  if (file->magic != FILE_MAGIC) {
    i32 written = write(1, pointer, wanted);
    return written < 0 ? 0 : (u32)written / size;
  }
#endif
  if (file->magic != FILE_MAGIC || !(file->flags & FILE_WRITE)) {
    if (file->magic == FILE_MAGIC) file->error = 1;
    return 0;
  }
#ifdef WASTE_POSIX_IO
  if (file->descriptor >= 0) {
    i32 written = write(file->descriptor, pointer, wanted);
    if (written < 0) { file->error = 1; return 0; }
    return (u32)written / size;
  }
#endif
  u32 position = (file->flags & FILE_APPEND) ? file->length : file->position;
  u32 available = position < file->capacity ? file->capacity - position : 0;
  u32 copied = wanted < available ? wanted : available;
  bytes_copy(file->data + position, pointer, copied);
  file->position = position + copied;
  if (file->position > file->length) file->length = file->position;
  if (copied != wanted) file->error = 1;
  return copied / size;
}

i32 fputc(i32 character, FILE *file) {
  unsigned char byte = (unsigned char)character;
  return fwrite(&byte, 1, 1, file) == 1 ? byte : EOF_VALUE;
}
i32 putc(i32 character, FILE *file) { return fputc(character, file); }

i32 fputs(const char *text, FILE *file) {
  u32 length = c_length(text);
  return fwrite(text, 1, length, file) == length ? (i32)length : EOF_VALUE;
}

i32 puts(const char *text) {
  if (!standard_output || fputs(text, standard_output) < 0) return EOF_VALUE;
  return fputc('\n', standard_output);
}

i32 putchar(i32 character) { return fputc(character, standard_output); }
i32 fflush(FILE *file) { (void)file; return 0; }
i32 fileno(FILE *file) {
#ifdef WASTE_POSIX_IO
  /* Bash owns its historical FILE objects.  They are opaque to this compact
     libc, and its readline path primarily asks for stdin's descriptor. */
  if (file && file->magic != FILE_MAGIC) return 0;
#endif
  return file && file->magic == FILE_MAGIC ? file->descriptor : -1;
}
i32 ferror(FILE *file) {
#ifdef WASTE_POSIX_IO
  if (file && file->magic != FILE_MAGIC) return 0;
#endif
  return file ? file->error : 1;
}
void clearerr(FILE *file) {
#ifdef WASTE_POSIX_IO
  if (file && file->magic != FILE_MAGIC) return;
#endif
  if (file) { file->error = 0; file->end_of_file = 0; }
}
i32 fpurge(FILE *file) {
  if (!file) return -1;
#ifdef WASTE_POSIX_IO
  if (file->magic != FILE_MAGIC) return 0;
#endif
  file->length = file->position = 0;
  return 0;
}
i32 __fpurge(FILE *file) { return fpurge(file); }
i32 setvbuf(FILE *file, char *buffer, i32 mode, u32 size) {
  (void)mode;
  if (!file || !buffer || !size) return -1;
#ifdef WASTE_POSIX_IO
  if (file->magic != FILE_MAGIC) return 0;
#endif
  file->data = (unsigned char *)buffer;
  file->capacity = size;
  file->length = file->position = 0;
  file->flags &= ~FILE_OWN_BUFFER;
  return 0;
}

char *fgets(char *destination, i32 count, FILE *file) {
  if (!destination || count <= 0 || !file) return 0;
#ifdef WASTE_POSIX_IO
  if (file->magic != FILE_MAGIC) {
    i32 received = read(0, destination, (u32)(count - 1));
    if (received <= 0) return 0;
    destination[received] = 0;
    return destination;
  }
#endif
  if (!(file->flags & FILE_READ)) return 0;
#ifdef WASTE_POSIX_IO
  if (file->position >= file->length && file->descriptor >= 0) {
    i32 received = read(file->descriptor, file->data, file->capacity);
    if (received < 0) { file->error = 1; return 0; }
    file->position = 0;
    file->length = (u32)received;
  }
#endif
  if (file->position >= file->length) { file->end_of_file = 1; return 0; }
  i32 written = 0;
  while (written + 1 < count && file->position < file->length) {
    char value = (char)file->data[file->position++];
    destination[written++] = value;
    if (value == '\n') break;
  }
  destination[written] = 0;
  return destination;
}

i32 fclose(FILE *file) {
  if (!file || file->magic != FILE_MAGIC || (file->flags & FILE_CLOSED)) return -1;
  file->flags |= FILE_CLOSED;
#ifdef WASTE_POSIX_IO
  if (file->descriptor > 2 && close(file->descriptor) < 0) file->error = 1;
#endif
  if (file->flags & FILE_OWN_BUFFER) free(file->data);
  file->magic = 0;
  free(file);
  return 0;
}

typedef struct FormatOutput { char *destination; u32 capacity; u32 count; } FormatOutput;

static void format_byte(FormatOutput *output, char value) {
  if (output->destination && output->capacity && output->count + 1 < output->capacity)
    output->destination[output->count] = value;
  output->count++;
}

static void format_text(FormatOutput *output, const char *text, i32 precision) {
  if (!text) {
    format_byte(output, '('); format_byte(output, 'n'); format_byte(output, 'u');
    format_byte(output, 'l'); format_byte(output, 'l'); format_byte(output, ')');
    return;
  }
  u32 at = 0;
  while (text[at] && (precision < 0 || at < (u32)precision)) format_byte(output, text[at++]);
}

static void format_unsigned(FormatOutput *output, u64 value, u32 radix,
                            i32 upper, i32 width, char padding, i32 negative) {
  char digits[32];
  u32 count = 0;
  do {
    u32 digit = (u32)(value % radix);
    digits[count++] = digit < 10 ? (char)('0' + digit) :
      (char)((upper ? 'A' : 'a') + digit - 10);
    value /= radix;
  } while (value && count < sizeof(digits));
  i32 total = (i32)count + negative;
  if (negative && padding == '0') format_byte(output, '-');
  while (total < width) { format_byte(output, padding); total++; }
  if (negative && padding != '0') format_byte(output, '-');
  while (count) format_byte(output, digits[--count]);
}

static i32 format_variadic(char *destination, u32 capacity, const char *format, va_list arguments) {
  FormatOutput output = {destination, capacity, 0};
  for (u32 at = 0; format && format[at]; at++) {
    if (format[at] != '%') { format_byte(&output, format[at]); continue; }
    at++;
    if (format[at] == '%') { format_byte(&output, '%'); continue; }
    char padding = ' ';
    if (format[at] == '0') { padding = '0'; at++; }
    i32 width = 0;
    if (format[at] == '*') { width = va_arg(arguments, i32); at++; }
    else while (format[at] >= '0' && format[at] <= '9') width = width * 10 + format[at++] - '0';
    i32 precision = -1;
    if (format[at] == '.') {
      at++; precision = 0;
      if (format[at] == '*') { precision = va_arg(arguments, i32); at++; }
      else while (format[at] >= '0' && format[at] <= '9') precision = precision * 10 + format[at++] - '0';
    }
    i32 long_count = 0;
    while (format[at] == 'l') { long_count++; at++; }
    char conversion = format[at];
    if (conversion == 's') format_text(&output, va_arg(arguments, const char *), precision);
    else if (conversion == 'c') format_byte(&output, (char)va_arg(arguments, i32));
    else if (conversion == 'd' || conversion == 'i') {
      i64 value = long_count > 1 ? va_arg(arguments, i64) :
        long_count ? (i64)va_arg(arguments, long) : (i64)va_arg(arguments, i32);
      i32 negative = value < 0;
      u64 magnitude = negative ? (u64)(-(value + 1)) + 1 : (u64)value;
      format_unsigned(&output, magnitude, 10, 0, width, padding, negative);
    } else if (conversion == 'u' || conversion == 'x' || conversion == 'X') {
      u64 value = long_count > 1 ? va_arg(arguments, u64) :
        long_count ? (u64)va_arg(arguments, unsigned long) : (u64)va_arg(arguments, u32);
      format_unsigned(&output, value, conversion == 'u' ? 10 : 16,
                      conversion == 'X', width, padding, 0);
    } else if (conversion == 'p') {
      format_byte(&output, '0'); format_byte(&output, 'x');
      format_unsigned(&output, (u32)(u64)va_arg(arguments, void *), 16, 0, 0, ' ', 0);
    } else {
      format_byte(&output, '%');
      if (conversion) format_byte(&output, conversion);
    }
  }
  if (destination && capacity) destination[output.count < capacity ? output.count : capacity - 1] = 0;
  return (i32)output.count;
}

i32 vsnprintf(char *destination, u32 capacity, const char *format, va_list arguments) {
  va_list copy; va_copy(copy, arguments);
  i32 result = format_variadic(destination, capacity, format, copy);
  va_end(copy); return result;
}

i32 snprintf(char *destination, u32 capacity, const char *format, ...) {
  va_list arguments; va_start(arguments, format);
  i32 result = format_variadic(destination, capacity, format, arguments);
  va_end(arguments); return result;
}

i32 sprintf(char *destination, const char *format, ...) {
  va_list arguments; va_start(arguments, format);
  i32 result = format_variadic(destination, 0xffffffffU, format, arguments);
  va_end(arguments); return result;
}

i32 vfprintf(FILE *file, const char *format, va_list arguments) {
  va_list copy; va_copy(copy, arguments);
  i32 length = format_variadic(0, 0, format, copy); va_end(copy);
  char *buffer = malloc((u32)length + 1); if (!buffer) return -1;
  va_copy(copy, arguments); format_variadic(buffer, (u32)length + 1, format, copy); va_end(copy);
  i32 result = fwrite(buffer, 1, (u32)length, file) == (u32)length ? length : -1;
  free(buffer); return result;
}

i32 fprintf(FILE *file, const char *format, ...) {
  va_list arguments; va_start(arguments, format);
  i32 result = vfprintf(file, format, arguments); va_end(arguments); return result;
}

i32 printf(const char *format, ...) {
  va_list arguments; va_start(arguments, format);
  i32 result = vfprintf(standard_output, format, arguments); va_end(arguments); return result;
}

i32 asprintf(char **destination, const char *format, ...) {
  va_list arguments, copy; va_start(arguments, format); va_copy(copy, arguments);
  i32 length = format_variadic(0, 0, format, copy); va_end(copy);
  char *buffer = malloc((u32)length + 1); if (!buffer) { va_end(arguments); return -1; }
  format_variadic(buffer, (u32)length + 1, format, arguments); va_end(arguments);
  *destination = buffer; return length;
}

#endif /* WASTE_ENGINE */
