#include <stdarg.h>

typedef unsigned int u32;
typedef signed int i32;
typedef unsigned long long u64;
typedef signed long long i64;

extern void *malloc(u32 size);
extern void free(void *pointer);
extern i32 *__errno_location(void);
#ifdef WASTE_POSIX_IO
extern i32 open(const char *path, i32 flags, i32 mode);
extern i32 close(i32 descriptor);
extern i32 read(i32 descriptor, void *buffer, u32 count);
extern i32 write(i32 descriptor, const void *buffer, u32 count);
#endif

static u32 c_length(const char *text) {
  u32 length = 0;
  if ((u32)text >= __builtin_wasm_memory_size(0) * 65536U) return 0;
  if (text) while (text[length]) length++;
  return length;
}

static i32 c_compare(const char *left, const char *right) {
  u32 at = 0;
  while (left[at] && left[at] == right[at]) at++;
  return (unsigned char)left[at] - (unsigned char)right[at];
}

static void bytes_copy(void *destination, const void *source, u32 count) {
  unsigned char *to = destination;
  const unsigned char *from = source;
  if (to < from) for (u32 at = 0; at < count; at++) to[at] = from[at];
  else while (count) { count--; to[count] = from[count]; }
}

static void bytes_zero(void *destination, u32 count) {
  unsigned char *to = destination;
  for (u32 at = 0; at < count; at++) to[at] = 0;
}

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
  if (file && file->magic != FILE_MAGIC) return 1;
#endif
  return file && file->magic == FILE_MAGIC ? file->descriptor : -1;
}
i32 ferror(FILE *file) { return file ? file->error : 1; }
void clearerr(FILE *file) { if (file) { file->error = 0; file->end_of_file = 0; } }
i32 fpurge(FILE *file) { if (!file) return -1; file->length = file->position = 0; return 0; }
i32 __fpurge(FILE *file) { return fpurge(file); }
i32 setvbuf(FILE *file, char *buffer, i32 mode, u32 size) {
  (void)mode;
  if (!file || !buffer || !size) return -1;
  file->data = (unsigned char *)buffer;
  file->capacity = size;
  file->length = file->position = 0;
  file->flags &= ~FILE_OWN_BUFFER;
  return 0;
}

char *fgets(char *destination, i32 count, FILE *file) {
  if (!destination || count <= 0 || !file || !(file->flags & FILE_READ)) return 0;
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

static i32 utf8_decode(const unsigned char *source, u32 available, u32 *codepoint) {
  if (!source) return 0;
  u32 first = source[0];
  if (first < 0x80) { *codepoint = first; return first ? 1 : 0; }
  u32 count = first < 0xe0 ? 2 : first < 0xf0 ? 3 : first < 0xf5 ? 4 : 0;
  if (!count || available < count) return -2;
  u32 value = first & (0x7fU >> count);
  for (u32 at = 1; at < count; at++) {
    if ((source[at] & 0xc0) != 0x80) return -1;
    value = (value << 6) | (source[at] & 0x3f);
  }
  if ((count == 2 && value < 0x80) || (count == 3 && value < 0x800) ||
      (count == 4 && value < 0x10000) || value > 0x10ffff ||
      (value >= 0xd800 && value <= 0xdfff)) return -1;
  *codepoint = value; return (i32)count;
}

static i32 utf8_encode(unsigned char *destination, u32 codepoint) {
  if (codepoint <= 0x7f) { destination[0] = (unsigned char)codepoint; return 1; }
  if (codepoint <= 0x7ff) {
    destination[0] = 0xc0 | (codepoint >> 6); destination[1] = 0x80 | (codepoint & 63); return 2;
  }
  if (codepoint >= 0xd800 && codepoint <= 0xdfff) return -1;
  if (codepoint <= 0xffff) {
    destination[0] = 0xe0 | (codepoint >> 12); destination[1] = 0x80 | ((codepoint >> 6) & 63);
    destination[2] = 0x80 | (codepoint & 63); return 3;
  }
  if (codepoint <= 0x10ffff) {
    destination[0] = 0xf0 | (codepoint >> 18); destination[1] = 0x80 | ((codepoint >> 12) & 63);
    destination[2] = 0x80 | ((codepoint >> 6) & 63); destination[3] = 0x80 | (codepoint & 63); return 4;
  }
  return -1;
}

i32 __ctype_get_mb_cur_max(void) { return 4; }
i32 mbrtowc(u32 *wide, const char *source, u32 available, void *state) {
  (void)state; u32 value = 0; i32 result = utf8_decode((const unsigned char *)source, available, &value);
  if (result == -1) *__errno_location() = 84; if (result >= 0 && wide) *wide = value; return result;
}
i32 mbtowc(u32 *wide, const char *source, u32 available) { return mbrtowc(wide, source, available, 0); }
i32 mbrlen(const char *source, u32 available, void *state) { return mbrtowc(0, source, available, state); }
i32 mblen(const char *source, u32 available) { return mbrtowc(0, source, available, 0); }
i32 wcrtomb(char *destination, u32 value, void *state) {
  (void)state; if (!destination) return 1; i32 result = utf8_encode((unsigned char *)destination, value);
  if (result < 0) *__errno_location() = 84; return result;
}

u32 mbstowcs(u32 *destination, const char *source, u32 capacity) {
  u32 count = 0, offset = 0;
  while (source[offset]) {
    u32 value; i32 used = utf8_decode((const unsigned char *)source + offset, 4, &value);
    if (used < 0) return 0xffffffffU;
    if (destination && count < capacity) destination[count] = value;
    count++; offset += (u32)used;
    if (destination && count == capacity) break;
  }
  if (destination && count < capacity) destination[count] = 0;
  return count;
}

u32 mbsrtowcs(u32 *destination, const char **source, u32 capacity, void *state) {
  (void)state; if (!source || !*source) return 0;
  u32 result = mbstowcs(destination, *source, capacity);
  if (result != 0xffffffffU && destination) *source = 0; return result;
}

u32 mbsnrtowcs(u32 *destination, const char **source, u32 bytes, u32 capacity, void *state) {
  (void)state; if (!source || !*source) return 0;
  u32 count = 0, offset = 0;
  while (offset < bytes && (*source)[offset] && count < capacity) {
    u32 value; i32 used = utf8_decode((const unsigned char *)*source + offset, bytes - offset, &value);
    if (used < 0) return 0xffffffffU;
    if (destination) destination[count] = value; count++; offset += (u32)used;
  }
  if (!(*source)[offset]) *source = 0; else *source += offset; return count;
}

u32 wcsrtombs(char *destination, const u32 **source, u32 capacity, void *state) {
  (void)state; if (!source || !*source) return 0; u32 written = 0;
  while (**source) {
    unsigned char encoded[4]; i32 count = utf8_encode(encoded, **source);
    if (count < 0) return 0xffffffffU;
    if (destination && written + (u32)count > capacity) break;
    if (destination) bytes_copy(destination + written, encoded, (u32)count);
    written += (u32)count; (*source)++;
  }
  if (**source == 0) { if (destination && written < capacity) destination[written] = 0; *source = 0; }
  return written;
}

u32 wcslen(const u32 *text) { u32 n = 0; while (text[n]) n++; return n; }
i32 wcscmp(const u32 *a, const u32 *b) { while (*a && *a == *b) { a++; b++; } return *a < *b ? -1 : *a > *b; }
i32 wcsncmp(const u32 *a, const u32 *b, u32 n) { while (n && *a && *a == *b) { a++; b++; n--; } return !n ? 0 : *a < *b ? -1 : *a > *b; }
i32 wcscoll(const u32 *a, const u32 *b) { return wcscmp(a, b); }
i32 strcoll(const char *a, const char *b) { return c_compare(a, b); }
u32 *wcschr(const u32 *text, u32 value) { while (*text && *text != value) text++; return *text == value ? (u32 *)text : 0; }
u32 *wmemchr(const u32 *text, u32 value, u32 n) { while (n--) { if (*text == value) return (u32 *)text; text++; } return 0; }
u32 *wcsdup(const u32 *text) { u32 n = wcslen(text) + 1, *copy = malloc(n * 4); if (copy) bytes_copy(copy, text, n * 4); return copy; }

i32 iscntrl(i32 c) { return (c >= 0 && c < 32) || c == 127; }
i32 isblank(i32 c) { return c == ' ' || c == '\t'; }
i32 isalnum(i32 c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
i32 ispunct(i32 c) { return c >= 33 && c <= 126 && !isalnum(c); }
i32 isxdigit(i32 c) { return (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'f'); }
i32 toupper(i32 c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
i32 tolower(i32 c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
i32 iswalnum(u32 c) { return c < 128 ? isalnum((i32)c) : c <= 0x10ffff; }
i32 iswupper(u32 c) { return c >= 'A' && c <= 'Z'; }
i32 iswlower(u32 c) { return c >= 'a' && c <= 'z'; }
i32 iswprint(u32 c) { return c >= 32 && c != 127 && c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff); }
u32 towupper(u32 c) { return (u32)toupper((i32)c); }
u32 towlower(u32 c) { return (u32)tolower((i32)c); }
i32 wctob(u32 c) { return c <= 0x7f ? (i32)c : -1; }
i32 wcwidth(u32 c) {
  if (!c) return 0; if (!iswprint(c)) return -1;
  if ((c >= 0x300 && c <= 0x36f) || (c >= 0x1ab0 && c <= 0x1aff)) return 0;
  if ((c >= 0x1100 && c <= 0x115f) || (c >= 0x2e80 && c <= 0xa4cf) ||
      (c >= 0xac00 && c <= 0xd7a3) || (c >= 0xf900 && c <= 0xfaff) || c >= 0x1f300) return 2;
  return 1;
}
i32 wcswidth(const u32 *text, u32 count) { i32 width = 0; while (count-- && *text) { i32 n = wcwidth(*text++); if (n < 0) return -1; width += n; } return width; }

static char *locale_name;
static char *empty_text;
static char *decimal_point;
static u32 *locale_record;
static char *domain_name;

static void locale_initialize(void) {
  if (locale_name) return;
  locale_name = malloc(8); empty_text = malloc(1); decimal_point = malloc(2);
  locale_name[0]='C'; locale_name[1]='.'; locale_name[2]='U'; locale_name[3]='T';
  locale_name[4]='F'; locale_name[5]='-'; locale_name[6]='8'; locale_name[7]=0;
  empty_text[0]=0; decimal_point[0]='.'; decimal_point[1]=0;
  locale_record = malloc(56); bytes_zero(locale_record, 56);
  locale_record[0]=(u32)decimal_point;
  for (u32 at=1; at<10; at++) locale_record[at]=(u32)empty_text;
  unsigned char *characters=(unsigned char *)(locale_record+10);
  for (u32 at=0; at<14; at++) characters[at]=127;
  domain_name=empty_text;
}

char *setlocale(i32 category, const char *locale) { (void)category; locale_initialize(); (void)locale; return locale_name; }
u32 *localeconv(void) { locale_initialize(); return locale_record; }
char *nl_langinfo(i32 item) { (void)item; locale_initialize(); return locale_name; }
char *gettext(const char *message) { return (char *)message; }
char *dgettext(const char *domain, const char *message) { (void)domain; return (char *)message; }
char *ngettext(const char *one, const char *many, u32 count) { return (char *)(count == 1 ? one : many); }
char *bindtextdomain(const char *domain, const char *directory) { (void)domain; return (char *)directory; }
char *textdomain(const char *domain) { locale_initialize(); if (domain && *domain) domain_name=(char *)domain; return domain_name; }
i32 mbsinit(const void *state) { (void)state; return 1; }
char *locale_charset(void) { locale_initialize(); return locale_name + 2; }
u32 iconv_open(const char *to_encoding, const char *from_encoding) {
  (void)to_encoding; (void)from_encoding; locale_initialize(); return 1;
}
i32 iconv_close(u32 descriptor) { return descriptor == 1 ? 0 : -1; }
u32 iconv(u32 descriptor, char **input, u32 *input_left, char **output, u32 *output_left) {
  if (descriptor != 1) { *__errno_location() = 22; return 0xffffffffU; }
  if (!input || !*input) return 0;
  u32 copied = *input_left < *output_left ? *input_left : *output_left;
  bytes_copy(*output, *input, copied);
  *input += copied; *input_left -= copied; *output += copied; *output_left -= copied;
  if (*input_left) { *__errno_location() = 7; return 0xffffffffU; }
  return 0;
}
u32 wctype(const char *name) {
  if (!name) return 0; if (!c_compare(name,"alnum")) return 1; if (!c_compare(name,"lower")) return 2;
  if (!c_compare(name,"upper")) return 3; if (!c_compare(name,"print")) return 4; return 0;
}
i32 iswctype(u32 value, u32 descriptor) {
  return descriptor==1 ? iswalnum(value) : descriptor==2 ? iswlower(value) :
    descriptor==3 ? iswupper(value) : descriptor==4 ? iswprint(value) : 0;
}

typedef struct WastePasswd { char *name; char *password; u32 uid; u32 gid; char *gecos; char *directory; char *shell; } WastePasswd;
typedef struct WasteGroup { char *name; char *password; u32 gid; char **members; } WasteGroup;
typedef struct WasteService { char *name; char **aliases; i32 port; char *protocol; } WasteService;
static u32 real_uid, effective_uid, real_gid, effective_gid;
static u32 *supplementary_groups; static u32 supplementary_count;
static WastePasswd passwd_record; static WasteGroup group_record; static WasteService service_record;
static i32 passwd_cursor, group_cursor, service_cursor;
static char *host_name;

void waste_identity_set(u32 uid, u32 euid, u32 gid, u32 egid, char *hostname) {
  real_uid=uid; effective_uid=euid; real_gid=gid; effective_gid=egid; host_name=hostname;
}
void waste_passwd_set(char *name,char *password,u32 uid,u32 gid,char *gecos,char *directory,char *shell) {
  passwd_record.name=name; passwd_record.password=password; passwd_record.uid=uid; passwd_record.gid=gid;
  passwd_record.gecos=gecos; passwd_record.directory=directory; passwd_record.shell=shell; passwd_cursor=0;
}
void waste_group_set(char *name,char *password,u32 gid,char **members) {
  group_record.name=name; group_record.password=password; group_record.gid=gid; group_record.members=members; group_cursor=0;
}
void waste_service_set(char *name,char **aliases,i32 port,char *protocol) {
  service_record.name=name; service_record.aliases=aliases; service_record.port=port; service_record.protocol=protocol; service_cursor=0;
}
i32 waste_groups_set(u32 count, const u32 *groups) {
  u32 *copy=malloc(count*4); if (count && !copy) return -1; if (count) bytes_copy(copy,groups,count*4);
  if (supplementary_groups) free(supplementary_groups); supplementary_groups=copy; supplementary_count=count; return 0;
}
u32 getuid(void){return real_uid;} u32 geteuid(void){return effective_uid;}
u32 getgid(void){return real_gid;} u32 getegid(void){return effective_gid;}
i32 setuid(u32 uid){if(effective_uid && uid!=real_uid&&uid!=effective_uid){*__errno_location()=1;return -1;}real_uid=effective_uid=uid;return 0;}
i32 setgid(u32 gid){if(effective_uid && gid!=real_gid&&gid!=effective_gid){*__errno_location()=1;return -1;}real_gid=effective_gid=gid;return 0;}
i32 getgroups(i32 capacity,u32 *groups){if(!capacity)return (i32)supplementary_count;if(capacity<(i32)supplementary_count){*__errno_location()=22;return -1;}bytes_copy(groups,supplementary_groups,supplementary_count*4);return (i32)supplementary_count;}
WastePasswd *getpwuid(u32 uid){return passwd_record.name&&passwd_record.uid==uid?&passwd_record:0;}
WastePasswd *getpwnam(const char *name){return passwd_record.name&&!c_compare(passwd_record.name,name)?&passwd_record:0;}
void setpwent(void){passwd_cursor=0;} WastePasswd *getpwent(void){if(passwd_cursor++||!passwd_record.name)return 0;return &passwd_record;} void endpwent(void){passwd_cursor=1;}
void setgrent(void){group_cursor=0;} WasteGroup *getgrent(void){if(group_cursor++||!group_record.name)return 0;return &group_record;} void endgrent(void){group_cursor=1;}
void setservent(i32 stayopen){(void)stayopen;service_cursor=0;} WasteService *getservent(void){if(service_cursor++||!service_record.name)return 0;return &service_record;} void endservent(void){service_cursor=1;}
i32 gethostname(char *destination,u32 capacity){if(!host_name||!capacity){*__errno_location()=22;return -1;}u32 n=c_length(host_name);if(n>=capacity){*__errno_location()=36;return -1;}bytes_copy(destination,host_name,n+1);return 0;}
