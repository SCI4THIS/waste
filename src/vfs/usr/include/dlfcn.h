/* dlfcn.h — POSIX dynamic loading for the WASTE guest libc. */

#ifndef WASTE_DLFCN_H
#define WASTE_DLFCN_H

#define RTLD_LAZY    1
#define RTLD_NOW     2
#define RTLD_GLOBAL  256
#define RTLD_LOCAL   0

#ifdef __cplusplus
extern "C" {
#endif

void *dlopen(const char *path, int flags);
void *dlsym(void *handle, const char *name);
int   dlclose(void *handle);
char *dlerror(void);

#ifdef __cplusplus
}
#endif

#endif /* WASTE_DLFCN_H */
