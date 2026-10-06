#ifndef WASTE_PTHREAD_H
#define WASTE_PTHREAD_H

#include <waste/abi/availability.h>

#include <signal.h>
#include <errno.h>
#include <time.h>

typedef unsigned long pthread_t;
typedef unsigned int pthread_key_t;
typedef int pthread_once_t;
typedef struct { unsigned long _words[8]; } pthread_mutex_t;
typedef int pthread_mutexattr_t;
typedef struct { unsigned long _words[8]; } pthread_cond_t;
typedef int pthread_condattr_t;
typedef int pthread_attr_t;
typedef struct { unsigned long _words[8]; } pthread_rwlock_t;
typedef int pthread_rwlockattr_t;

#define PTHREAD_MUTEX_INITIALIZER { { 0 } }
#define PTHREAD_COND_INITIALIZER { { 0 } }
#define PTHREAD_ONCE_INIT 0
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_MUTEX_STALLED 0
#define PTHREAD_MUTEX_ROBUST 1
#define PTHREAD_MUTEX_DEFAULT 0
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_ERRORCHECK 1
#define PTHREAD_MUTEX_RECURSIVE 2

int pthread_sigmask(int, const sigset_t *, sigset_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int pthread_mutex_destroy(pthread_mutex_t *);
int pthread_mutex_lock(pthread_mutex_t *);
int pthread_mutex_unlock(pthread_mutex_t *);
int pthread_mutex_trylock(pthread_mutex_t *);
int pthread_mutex_timedlock(pthread_mutex_t *, const struct timespec *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_mutexattr_init(pthread_mutexattr_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_mutexattr_destroy(pthread_mutexattr_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_mutexattr_settype(pthread_mutexattr_t *, int) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_cond_destroy(pthread_cond_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_cond_signal(pthread_cond_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_cond_broadcast(pthread_cond_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_rwlock_init(pthread_rwlock_t *, const pthread_rwlockattr_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_rwlock_destroy(pthread_rwlock_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_rwlock_rdlock(pthread_rwlock_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_rwlock_wrlock(pthread_rwlock_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_rwlock_unlock(pthread_rwlock_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_rwlockattr_init(pthread_rwlockattr_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_rwlockattr_destroy(pthread_rwlockattr_t *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_rwlockattr_setkind_np(pthread_rwlockattr_t *, int) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_join(pthread_t, void **) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
pthread_t pthread_self(void) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_key_create(pthread_key_t *, void (*)(void *)) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_key_delete(pthread_key_t) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
void *pthread_getspecific(pthread_key_t) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_setspecific(pthread_key_t, const void *) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");
int pthread_once(pthread_once_t *, void (*)(void)) WASTE_UNAVAILABLE("No WASTE runtime provider; declaration only, not a supported guest capability");

#endif
