#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "execute.h"

#define ERR(...) fprintf(stderr, "%s: %d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__)

int main(int argc, char **argv)
{
  FILE *f;
  size_t fsize;
  unsigned char *buf;
  int nread;
  int totread;
  const char *res;
  if (argc < 2) {
    printf("usage: %s <file> [<arg> ... <arg>]\n", argv[0]);
    return 1;
  }
  f = fopen(argv[1], "rb");
  if (!f) {
    ERR("fopen(%s, \"rb\"): %s\n", argv[1], strerror(errno));
    return 2;
  }
  fseek(f, 0, SEEK_END);
  fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  buf = malloc(fsize);
  totread = 0;
  while (totread < fsize && (nread = fread(&buf[totread], 1, fsize - totread, f)) != 0) {
    totread += nread;
  }
  res = execute_wasm(buf, fsize, argc - 1, &argv[1]);
  if (res != NULL && strcmp(res, "Invalid WASM") != 0) {
    goto done;
  }
  //res = execute_wast(buf, fsize, argc - 1, &argv[1]);

done:
  if (res != NULL) {
    ERR("execute_wasm: %s\n", res);
    return 3;
  }
  fclose(f);
  return 0;
}
