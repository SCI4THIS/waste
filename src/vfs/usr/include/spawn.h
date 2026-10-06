#ifndef WASTE_SPAWN_H
#define WASTE_SPAWN_H

#include <waste/abi/availability.h>

#include <sched.h>
#include <sys/select.h>
#include <sys/types.h>

/* The engine-owned process ABI keeps these objects opaque to the guest.  The
 * storage is deliberately fixed-size for source compatibility; execution
 * support will be added when process spawning is wired to the kernel. */
typedef struct {
  short _flags;
  pid_t _pgrp;
  sigset_t _sd;
  sigset_t _ss;
  struct sched_param _sp;
  int _policy;
  int _reserved[8];
} posix_spawnattr_t;

typedef struct {
  int _allocated;
  int _used;
  void *_actions;
  int __reserved[8];
} posix_spawn_file_actions_t;

#define POSIX_SPAWN_RESETIDS 0x01
#define POSIX_SPAWN_SETPGROUP 0x02
#define POSIX_SPAWN_SETSIGDEF 0x04
#define POSIX_SPAWN_SETSIGMASK 0x08
#define POSIX_SPAWN_SETSCHEDPARAM 0x10
#define POSIX_SPAWN_SETSCHEDULER 0x20
#define POSIX_SPAWN_USEVFORK 0x40

int posix_spawn(pid_t *, const char *, const posix_spawn_file_actions_t *,
                const posix_spawnattr_t *, char *const [], char *const []) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int posix_spawnp(pid_t *, const char *, const posix_spawn_file_actions_t *,
                 const posix_spawnattr_t *, char *const [], char *const []) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int posix_spawnattr_init(posix_spawnattr_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int posix_spawnattr_destroy(posix_spawnattr_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int posix_spawnattr_setflags(posix_spawnattr_t *, short) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int posix_spawnattr_getflags(const posix_spawnattr_t *, short *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int posix_spawnattr_setsigdefault(posix_spawnattr_t *, const sigset_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int posix_spawnattr_getsigdefault(const posix_spawnattr_t *, sigset_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int posix_spawnattr_setsigmask(posix_spawnattr_t *, const sigset_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int posix_spawnattr_getsigmask(const posix_spawnattr_t *, sigset_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int posix_spawn_file_actions_init(posix_spawn_file_actions_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *, int) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *, int, int) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *, int,
                                      const char *, int, mode_t) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");

#endif
