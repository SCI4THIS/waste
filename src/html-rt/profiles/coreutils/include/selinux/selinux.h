#ifndef WASTE_SELINUX_SELINUX_H
#define WASTE_SELINUX_SELINUX_H

/* SELinux is intentionally unavailable in the engine-owned browser ABI. */
#define WASTE_SELINUX_UNAVAILABLE 1

int getfilecon(const char *, char **);
int lgetfilecon(const char *, char **);
int fgetfilecon(int, char **);
int freecon(char *);
int setfilecon(const char *, const char *);
int lsetfilecon(const char *, const char *);

#endif
