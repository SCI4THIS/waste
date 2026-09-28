#ifndef POSIX_STUBS_H
#define POSIX_STUBS_H

#include "store.h"

#include <stdint.h>

int browser_host_resolver(const char *module, const char *name,
                           void *context, native_host_binding *out);
uint64_t waste_browser_realtime_now(void *data);
#endif /* POSIX_STUBS_H */
