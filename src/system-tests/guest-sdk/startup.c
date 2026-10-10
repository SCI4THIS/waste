/* Stage 8D startup fixture.  It proves that the CRT receives the engine-owned
 * startup block and maps main's return value through env.exit. */
typedef unsigned int u32;

__attribute__((import_module("env"), import_name("write")))
extern int waste_write(int fd, const void *bytes, u32 length);

/* This small CRT-link fixture uses waste-crt.c without the full guest libc.
 * The CRT still binds its standard-stream slots before main, so provide the
 * three ABI symbols it expects. The hello main writes through env.write and
 * does not use stdio; production utilities resolve these from waste-libc. */
typedef struct WasteFile WasteFile;
WasteFile *waste_stdin(void) { return (WasteFile *)0; }
WasteFile *waste_stdout(void) { return (WasteFile *)0; }
WasteFile *waste_stderr(void) { return (WasteFile *)0; }
int fflush(void *stream) { (void)stream; return 0; }

static u32 text_length(const char *text) {
  u32 length = 0;
  while (text[length]) length++;
  return length;
}

static void write_text(const char *text) {
  waste_write(1, text, text_length(text));
}

int main(int argc, char **argv, char **envp) {
  if (!argv || !envp) return 120;
  write_text("WASTE_SYSROOT_HELLO\n");
  return argc > 1 ? 7 : 0;
}
