/* Guest stdio: FILE management, I/O operations, and the printf family. */

/* ---- Guest libc: full FILE I/O and printf ---- */
#include "include/helper.h"

#ifdef WASTE_POSIX_IO
extern i32 open(const char *path, i32 flags, ...);
extern i32 close(i32 descriptor);
extern i32 read(i32 descriptor, void *buffer, u32 count);
extern i32 write(i32 descriptor, const void *buffer, u32 count);
extern i32 lseek(i32 descriptor, i32 offset, i32 whence);
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

#ifdef WASTE_SHARED_LIBC
/* PIC clients resolve these addresses through GOT.mem. Static test/app
 * profiles retain the CRT's pointer slots and bind them explicitly. */
FILE *stdin;
FILE *stdout;
FILE *stderr;
#define standard_input stdin
#define standard_output stdout
#define standard_error stderr
#else
static FILE *standard_input;
static FILE *standard_output;
static FILE *standard_error;
#endif

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

i32 fread(void *pointer, u32 size, u32 members, FILE *file) {
  if (!file || !pointer || !size) return 0;
  u64 wanted64 = (u64)size * members;
  if (wanted64 > 0xffffffffULL) return 0;
  u32 wanted = (u32)wanted64;
#ifdef WASTE_POSIX_IO
  /* The prebuilt Bash image owns a libc FILE layout that is intentionally
     opaque to this compact guest libc.  Readline reaches that stream through
     fgetc/fread, so route its stdin token to the process terminal just as the
     existing opaque fgets path does. */
  if (file->magic != FILE_MAGIC) {
    i32 received = read(0, pointer, wanted);
    return received < 0 ? 0 : (u32)received / size;
  }
#endif
  if (file->magic != FILE_MAGIC || !(file->flags & FILE_READ)) return 0;
  u32 copied = 0;
  while (copied < wanted) {
#ifdef WASTE_POSIX_IO
    if (file->position >= file->length && file->descriptor >= 0) {
      i32 received = read(file->descriptor, file->data, file->capacity);
      if (received < 0) { file->error = 1; break; }
      file->position = 0; file->length = (u32)received;
    }
#endif
    if (file->position >= file->length) {
      file->end_of_file = 1; break;
    }
    ((unsigned char *)pointer)[copied++] = file->data[file->position++];
  }
  return copied / size;
}

i32 fgetc(FILE *file) {
  unsigned char value;
  return fread(&value, 1, 1, file) == 1 ? value : EOF_VALUE;
}
i32 getc(FILE *file) { return fgetc(file); }
i32 feof(FILE *file) { return file ? file->end_of_file : 0; }

i32 getdelim(char **line, u32 *capacity, i32 delimiter, FILE *file) {
  if (!line || !capacity || !file) { *__errno_location() = 22; return -1; }
  if (!*line || *capacity < 2) {
    u32 next = *capacity < 2 ? 128 : *capacity;
    char *replacement = *line ? realloc(*line, next) : malloc(next);
    if (!replacement) { *__errno_location() = 12; return -1; }
    *line = replacement; *capacity = next;
  }
  u32 length = 0;
  for (;;) {
    i32 value = fgetc(file);
    if (value == EOF_VALUE) {
      if (!length) return -1;
      break;
    }
    if (length + 1 >= *capacity) {
      u32 next = *capacity > 0x7fffffffU / 2 ? 0xffffffffU : *capacity * 2;
      char *replacement = realloc(*line, next);
      if (!replacement) { *__errno_location() = 12; return -1; }
      *line = replacement; *capacity = next;
    }
    (*line)[length++] = (char)value;
    if (value == delimiter) break;
  }
  (*line)[length] = 0;
  return length;
}

i32 getline(char **line, u32 *capacity, FILE *file) {
  return getdelim(line, capacity, '\n', file);
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
void __fpurge(FILE *file) { (void)fpurge(file); }
void __fseterr(FILE *file) { if (file) file->error = 1; }
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

void setbuf(FILE *file, char *buffer) {
  if (buffer) (void)setvbuf(file, buffer, 0, 4096);
}

i32 fseek(FILE *file, long offset, i32 whence) {
  if (!file || file->magic != FILE_MAGIC) return -1;
#ifdef WASTE_POSIX_IO
  if (file->descriptor >= 0) {
    i32 position = lseek(file->descriptor, (i32)offset, whence);
    if (position < 0) { file->error = 1; return -1; }
    file->position = (u32)position;
    file->end_of_file = 0;
    return 0;
  }
#endif
  long base = whence == 1 ? (long)file->position :
              whence == 2 ? (long)file->length : 0;
  long position = base + offset;
  if (position < 0 || (u32)position > file->length) return -1;
  file->position = (u32)position;
  file->end_of_file = 0;
  return 0;
}

i32 fseeko(FILE *file, i32 offset, i32 whence) {
  return fseek(file, (long)offset, whence);
}

long ftell(FILE *file) {
  if (!file || file->magic != FILE_MAGIC) return -1;
#ifdef WASTE_POSIX_IO
  if (file->descriptor >= 0) {
    i32 position = lseek(file->descriptor, 0, 1);
    if (position < 0) { file->error = 1; return -1; }
    file->position = (u32)position;
  }
#endif
  return (long)file->position;
}

i32 ftello(FILE *file) { return (i32)ftell(file); }

void rewind(FILE *file) {
  if (fseek(file, 0, 0) == 0) clearerr(file);
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
    /* Skip flag characters: -, +, space, #, 0, ' (grouping) */
    char padding = ' ';
    i32 left_justify = 0, show_sign = 0, space_sign = 0;
    for (;;) {
      if (format[at] == '-') { left_justify = 1; at++; }
      else if (format[at] == '+') { show_sign = 1; at++; }
      else if (format[at] == ' ') { space_sign = 1; at++; }
      else if (format[at] == '#') { at++; }
      else if (format[at] == '\'') { at++; }
      else if (format[at] == '0') { padding = '0'; at++; }
      else break;
    }
    if (left_justify) padding = ' ';
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
    if (format[at] == 'j') { long_count = 2; at++; }
    else if (format[at] == 'z' || format[at] == 't') { at++; }
    else if (format[at] == 'h') { at++; if (format[at] == 'h') at++; }
    char conversion = format[at];
    if (conversion == 's') format_text(&output, va_arg(arguments, const char *), precision);
    else if (conversion == 'c') format_byte(&output, (char)va_arg(arguments, i32));
    else if (conversion == 'd' || conversion == 'i') {
      i64 value = long_count > 1 ? va_arg(arguments, i64) :
        long_count ? (i64)va_arg(arguments, long) : (i64)va_arg(arguments, i32);
      i32 negative = value < 0;
      u64 magnitude = negative ? (u64)(-(value + 1)) + 1 : (u64)value;
      format_unsigned(&output, magnitude, 10, 0, width, padding, negative);
    } else if (conversion == 'u' || conversion == 'o' || conversion == 'x' || conversion == 'X') {
      u64 value = long_count > 1 ? va_arg(arguments, u64) :
        long_count ? (u64)va_arg(arguments, unsigned long) : (u64)va_arg(arguments, u32);
      format_unsigned(&output, value, conversion == 'u' ? 10 : conversion == 'o' ? 8 : 16,
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

i32 vsprintf(char *destination, const char *format, va_list arguments) {
  return vsnprintf(destination, 0xffffffffU, format, arguments);
}

static i32 scan_space(i32 character) {
  return character == ' ' || character == '\t' || character == '\n' ||
         character == '\r' || character == '\f' || character == '\v';
}

static i32 scan_digit(i32 character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

static u32 scan_integer(const char **input, i32 base, i32 *negative,
                        i32 *matched) {
  const char *cursor = *input;
  *negative = 0;
  if (*cursor == '+' || *cursor == '-') {
    *negative = *cursor == '-';
    cursor++;
  }
  if (base == 0) {
    base = 10;
    if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
      base = 16; cursor += 2;
    } else if (cursor[0] == '0') base = 8;
  } else if (base == 16 && cursor[0] == '0' &&
             (cursor[1] == 'x' || cursor[1] == 'X')) cursor += 2;
  u32 value = 0;
  i32 count = 0;
  for (;;) {
    i32 digit = scan_digit((unsigned char)*cursor);
    if (digit < 0 || digit >= base) break;
    value = value * (u32)base + (u32)digit;
    cursor++; count++;
  }
  *matched = count != 0;
  if (*matched) *input = cursor;
  return value;
}

i32 vsscanf(const char *input, const char *format, va_list arguments) {
  i32 assigned = 0;
  const char *cursor = input;
  while (*format) {
    if (scan_space((unsigned char)*format)) {
      while (scan_space((unsigned char)*format)) format++;
      while (scan_space((unsigned char)*cursor)) cursor++;
      continue;
    }
    if (*format != '%') {
      if (*cursor != *format) break;
      cursor++; format++; continue;
    }
    format++;
    if (*format == '%') {
      if (*cursor != '%') break;
      cursor++; format++; continue;
    }
    i32 suppress = 0, width = 0, short_value = 0, long_value = 0;
    if (*format == '*') { suppress = 1; format++; }
    while (*format >= '0' && *format <= '9')
      width = width * 10 + (*format++ - '0');
    if (*format == 'h') { short_value = 1; format++; }
    else if (*format == 'l') { long_value = 1; format++; }
    i32 conversion = (unsigned char)*format++;
    if (conversion != 'c' && conversion != 'n')
      while (scan_space((unsigned char)*cursor)) cursor++;
    if (conversion == 's') {
      if (!*cursor) break;
      char *destination = suppress ? 0 : va_arg(arguments, char *);
      i32 count = 0;
      while (*cursor && !scan_space((unsigned char)*cursor) &&
             (!width || count < width)) {
        if (destination) destination[count] = *cursor;
        cursor++; count++;
      }
      if (!count) break;
      if (destination) { destination[count] = 0; assigned++; }
    } else if (conversion == 'c') {
      i32 count = width ? width : 1;
      if (!*cursor) break;
      char *destination = suppress ? 0 : va_arg(arguments, char *);
      for (i32 i = 0; i < count; i++) {
        if (!*cursor) return assigned;
        if (destination) destination[i] = *cursor;
        cursor++;
      }
      if (destination) assigned++;
    } else if (conversion == 'n') {
      if (!suppress) *va_arg(arguments, i32 *) = (i32)(cursor - input);
    } else {
      i32 base = conversion == 'x' || conversion == 'X' ? 16 :
                 conversion == 'o' ? 8 : conversion == 'i' ? 0 : 10;
      i32 negative = 0, matched = 0;
      u32 value = scan_integer(&cursor, base, &negative, &matched);
      if (!matched) break;
      if (!suppress) {
        void *destination = va_arg(arguments, void *);
        i32 signed_conversion = conversion == 'd' || conversion == 'i';
        i32 result = negative ? -(i32)value : (i32)value;
        if (short_value) *(unsigned short *)destination =
            (unsigned short)(signed_conversion ? result : (i32)value);
        else if (long_value) *(long *)destination =
            signed_conversion ? (long)result : (long)value;
        else *(i32 *)destination = signed_conversion ? result : (i32)value;
        assigned++;
      }
    }
  }
  return assigned;
}

i32 sscanf(const char *input, const char *format, ...) {
  va_list arguments; va_start(arguments, format);
  i32 result = vsscanf(input, format, arguments);
  va_end(arguments); return result;
}

void perror(const char *prefix) {
  extern char *strerror(i32);
  if (prefix && *prefix) { fputs(prefix, standard_error); fputs(": ", standard_error); }
  fputs(strerror(*__errno_location()), standard_error);
  fputc('\n', standard_error);
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

i32 vprintf(const char *format, va_list arguments) {
  return vfprintf(standard_output, format, arguments);
}

i32 vasprintf(char **destination, const char *format, va_list arguments) {
  va_list copy; va_copy(copy, arguments);
  i32 length = format_variadic(0, 0, format, copy); va_end(copy);
  char *buffer = malloc((u32)length + 1); if (!buffer) return -1;
  format_variadic(buffer, (u32)length + 1, format, arguments);
  *destination = buffer; return length;
}

/* gnulib-compatible *zprintf family — these delegate to waste-libc's own
   format_variadic instead of gnulib's vasnprintf, avoiding crashes from
   unresolved gnulib internal data structures in the wasm environment. */
typedef signed long off64_t;

off64_t vfzprintf(FILE *file, const char *format, va_list arguments) {
  return (off64_t)vfprintf(file, format, arguments);
}

off64_t vzprintf(const char *format, va_list arguments) {
  return vfzprintf(standard_output, format, arguments);
}

i32 vsnzprintf(char *destination, u32 capacity, const char *format, va_list arguments) {
  return vsnprintf(destination, capacity, format, arguments);
}

i32 vszprintf(char *destination, const char *format, va_list arguments) {
  return format_variadic(destination, 0xffffffffU, format, arguments);
}

i32 vaszprintf(char **destination, const char *format, va_list arguments) {
  return vasprintf(destination, format, arguments);
}

i32 aszprintf(char **destination, const char *format, ...) {
  va_list arguments; va_start(arguments, format);
  i32 result = vasprintf(destination, format, arguments);
  va_end(arguments); return result;
}
