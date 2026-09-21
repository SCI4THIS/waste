/* Minimal external-image ABI probe.  It intentionally depends only on the
 * stable env.write import so exec-image work can be tested before coreutils. */
typedef unsigned int u32;

__attribute__((import_module("env"), import_name("write")))
extern int waste_probe_write(int fd, const void *buffer, u32 length);

static const char probe_message[] = "WASTE_PROBE_OK\n";

__attribute__((export_name("_start")))
int waste_probe_start(void) {
  return waste_probe_write(1, probe_message, sizeof(probe_message) - 1) < 0 ? 1 : 0;
}
