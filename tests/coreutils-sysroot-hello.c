/* Stage 8D startup fixture.  It proves that the CRT receives the engine-owned
 * startup block and maps main's return value through env.exit. */
typedef unsigned int u32;

__attribute__((import_module("env"), import_name("write")))
extern int waste_write(int fd, const void *bytes, u32 length);

static u32 text_length(const char *text) {
  u32 length = 0;
  while (text[length]) length++;
  return length;
}

static void write_text(const char *text) {
  waste_write(1, text, text_length(text));
}

int main(int argc, char **argv, char **envp) {
  (void)argv;
  (void)envp;
  write_text("WASTE_SYSROOT_HELLO\n");
  return argc > 1 ? 7 : 0;
}
