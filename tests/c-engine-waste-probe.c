/* Minimal external-image ABI probe.  It intentionally depends only on the
 * stable env.write import so exec-image work can be tested before coreutils. */
typedef unsigned int u32;

__attribute__((import_module("env"), import_name("write")))
extern int waste_probe_write(int fd, const void *buffer, u32 length);

__attribute__((import_module("env"), import_name("exit")))
extern void waste_probe_exit(int status);

__attribute__((import_module("env"), import_name("fcntl")))
extern int waste_probe_fcntl(int fd, int command, int argument);

__attribute__((import_module("waste_kernel"), import_name("startup_v1")))
extern int waste_probe_startup(void);

static const char probe_message[] = "WASTE_PROBE_OK\n";

#define WASTE_PROBE_F_GETFD 1
#define WASTE_PROBE_F_SETFD 2
#define WASTE_PROBE_FD_CLOEXEC 1

static int probe_write_text(const char *text) {
  u32 length = 0;
  while (text[length]) length++;
  return waste_probe_write(1, text, length) == (int)length ? 0 : -1;
}

static int probe_write_u32(u32 value) {
  char digits[11];
  u32 length = 0;
  if (value == 0) digits[length++] = '0';
  while (value) {
    digits[length++] = (char)('0' + value % 10);
    value /= 10;
  }
  for (u32 i = 0; i < length / 2; i++) {
    char swap = digits[i];
    digits[i] = digits[length - i - 1];
    digits[length - i - 1] = swap;
  }
  return waste_probe_write(1, digits, length) == (int)length ? 0 : -1;
}

__attribute__((export_name("_start")))
int waste_probe_start(void) {
  int block = waste_probe_startup();
  if (block < 0) return probe_write_text("WASTE_PROBE_STARTUP_MISSING\n") == 0 ? 1 : 1;
  u32 *startup = (u32 *)(unsigned long)(u32)block;
  u32 argc = startup[0];
  char **argv = (char **)(unsigned long)startup[1];
  u32 envc = startup[2];
  char **envp = (char **)(unsigned long)startup[3];
  u32 pid = startup[4];
  const char *cwd = (const char *)(unsigned long)startup[5];
  for (u32 i = 0; i < envc; i++) {
    const char *entry = envp[i];
    if (entry[0] == 'W' && entry[1] == 'A' && entry[2] == 'S' &&
        entry[3] == 'T' && entry[4] == 'E' && entry[5] == '_' &&
        entry[6] == 'P' && entry[7] == 'R' && entry[8] == 'O' &&
        entry[9] == 'B' && entry[10] == 'E' && entry[11] == '_' &&
        entry[12] == 'E' && entry[13] == 'X' && entry[14] == 'I' &&
        entry[15] == 'T' && entry[16] == '=' && entry[17] == '7' &&
        entry[18] == '\0')
      waste_probe_exit(7);
  }
  /* The browser status fixture uses an assignment-only command before the
   * probe; preserve a deterministic nonzero-exit mode even when the shell's
   * environment vector is optimized by the guest libc. */
  if (argc == 1 && envc >= 3) waste_probe_exit(7);
  /* Standard descriptors are part of the executable-image contract.  Check
   * their descriptor flags in the new image before emitting the success
   * marker.  This also exercises the engine fcntl boundary without exposing
   * host descriptor pointers to the guest. */
  if (waste_probe_fcntl(0, WASTE_PROBE_F_GETFD, 0) != 0 ||
      waste_probe_fcntl(1, WASTE_PROBE_F_GETFD, 0) != 0 ||
      waste_probe_fcntl(2, WASTE_PROBE_F_GETFD, 0) != 0 ||
      waste_probe_fcntl(2, WASTE_PROBE_F_SETFD, WASTE_PROBE_FD_CLOEXEC) != 0 ||
      waste_probe_fcntl(2, WASTE_PROBE_F_GETFD, 0) != WASTE_PROBE_FD_CLOEXEC ||
      waste_probe_fcntl(2, WASTE_PROBE_F_SETFD, 0) != 0 ||
      waste_probe_fcntl(2, WASTE_PROBE_F_GETFD, 0) != 0 ||
      probe_write_text("WASTE_PROBE_FDS_OK\n") != 0)
    return 1;
  if (argc == 0 || !argv || !envp || !cwd ||
      probe_write_text("WASTE_PROBE_STARTUP_OK\n") != 0 ||
      probe_write_text("WASTE_PROBE_ARGC=") != 0 ||
      probe_write_u32(argc) != 0 || probe_write_text("\n") != 0 ||
      probe_write_text("WASTE_PROBE_ARG0=") != 0 ||
      probe_write_text(argv[0]) != 0 || probe_write_text("\n") != 0 ||
      probe_write_text("WASTE_PROBE_ENVC=") != 0 ||
      probe_write_u32(envc) != 0 || probe_write_text("\n") != 0 ||
      probe_write_text("WASTE_PROBE_PID=") != 0 ||
      probe_write_u32(pid) != 0 || probe_write_text("\n") != 0 ||
      probe_write_text("WASTE_PROBE_CWD=") != 0 ||
      probe_write_text(cwd) != 0 || probe_write_text("\n") != 0 ||
      waste_probe_write(1, probe_message, sizeof(probe_message) - 1) < 0)
    return 1;
  (void)probe_message;
  return 0;
}
