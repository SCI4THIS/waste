#ifndef POSIX_STUBS_H
#define POSIX_STUBS_H

#include "wast_linker.h"

int browser_host_resolver(const char *module, const char *name,
                           void *context, native_host_binding *out);

#endif /* POSIX_STUBS_H */
