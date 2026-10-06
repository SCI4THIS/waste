/* Guest batch launcher: bounded ABI request, guest descriptor output/results.
 * Deliberately freestanding; it does not require an additional libc provider. */
typedef unsigned int u32;
typedef unsigned char u8;
__attribute__((import_module("waste_kernel"), import_name("startup_v1")))
extern int startup(void);
__attribute__((import_module("waste_kernel"), import_name("test_suite_v1")))
extern int suite(const void *, u32, void *, u32);
__attribute__((import_module("env"), import_name("write")))
extern int write_fd(int, const void *, u32);
__attribute__((import_module("waste_kernel"), import_name("open_v1")))
extern int open_fd(const char *, int, int);
__attribute__((import_module("env"), import_name("close")))
extern int close_fd(int);
__attribute__((import_module("env"), import_name("exit")))
extern void exit_guest(int);
static u8 request[4096], reply[16 * 1024 * 1024];
static const char usage[] = "usage: waste-test [--list] [--group=NAME] [FILE...] [--exclude=FILE]\n"
 "       [--exclude-group=NAME] [--jobs=1..8] [--timeout-ms=N]\n"
 "       [--timeout-group=NAME:N] [--json] [--results=GUEST_PATH]\n";
static u32 length(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
static int equal(const char *a, const char *b) {
 while (*a && *a == *b) { a++; b++; } return *a == *b;
}
static int starts(const char *a, const char *b) {
 while (*b) if (*a++ != *b++) return 0; return 1;
}
static int write_all(int fd, const u8 *bytes, u32 n) {
 while (n) { int done = write_fd(fd, bytes, n > 32768 ? 32768 : n);
  if (done <= 0 || (u32)done > n) return -1; bytes += done; n -= (u32)done; }
 return 0;
}
static u32 word(const u8 *b) { return b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24; }
static void fail(const char *text) { write_all(2, (const u8 *)text, length(text)); exit_guest(2); }
__attribute__((export_name("_start")))
int start(void) {
 int block = startup();
 if (block < 0) { fail("waste-test: startup unavailable\n"); return 2; }
 const u32 *state = (const u32 *)(unsigned long)(u32)block;
 u32 argc = state[0]; const char **argv = (const char **)(unsigned long)state[1];
 if (argc > 65) { fail("waste-test: too many arguments\n"); return 2; }
 const char *result_path = 0; u32 size = 4; request[0] = 1;
 for (u32 i = 1; i < argc; i++) {
  if (equal(argv[i], "--help")) { write_all(1, (const u8 *)usage, sizeof(usage)-1); exit_guest(0); return 0; }
  if (starts(argv[i], "--results=")) {
   if (result_path || !argv[i][10] || length(argv[i]+10) >= 256) { fail(usage); return 2; }
   result_path = argv[i]+10; continue;
  }
  u32 n = length(argv[i]);
  if (!n || n >= sizeof(request)-size) { fail("waste-test: arguments exceed 4096 bytes\n"); return 2; }
  for (u32 j = 0; j <= n; j++) request[size++] = (u8)argv[i][j];
 }
 int got = suite(request, size, reply, sizeof(reply));
 if (got < 16 || word(reply) != 1) { fail("waste-test: batch capability unavailable or invalid request\n"); return 2; }
 u32 status = word(reply+4), out = word(reply+8), json = word(reply+12);
 if (out > (u32)got-16 || json != (u32)got-16-out ||
     (status != 0 && status != 1 && status != 2 && status != 130)) {
  fail("waste-test: invalid batch response\n"); return 2;
 }
 if (status != 2 && !json) { fail("waste-test: no completed batch report\n"); return 2; }
 if (write_all(status == 2 ? 2 : 1, reply+16, out)) { fail("waste-test: cannot write output\n"); return 2; }
 if (result_path) {
  if (!json) { fail("waste-test: no completed batch report\n"); return 2; }
  int fd = open_fd(result_path, 577, 0644);
  if (fd < 0) { fail("waste-test: cannot open guest result file\n"); return 2; }
  int bad = write_all(fd, reply+16+out, json); bad |= close_fd(fd);
  if (bad) { fail("waste-test: cannot write guest result file\n"); return 2; }
 }
 exit_guest((int)status); return (int)status;
}
