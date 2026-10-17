/* Process-image-owned exit registrations, shared by C and compiler C++ ABI calls.
 * The normal evaluator owns callback calls and their yielded continuations. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct exit_registration {
  struct exit_registration *next;
  void (*destructor)(void *);
  void (*callback)(void);
  void *argument;
  void *dso;
} exit_registration;

static exit_registration *registrations;

/* The engine initializes a new image's heap only after exec commits. Nodes
   from the discarded heap must neither be freed nor invoked in that image. */
void waste_exit_reset(void) { registrations = NULL; }

int __cxa_atexit(void (*destructor)(void *), void *argument, void *dso) {
  exit_registration *entry;
  if (!destructor) { errno = EINVAL; return -1; }
  entry = malloc(sizeof(*entry));
  if (!entry) { errno = ENOMEM; return -1; }
  *entry = (exit_registration){registrations, destructor, NULL, argument, dso};
  registrations = entry;
  return 0;
}

int atexit(void (*callback)(void)) {
  exit_registration *entry;
  if (!callback) { errno = EINVAL; return -1; }
  entry = malloc(sizeof(*entry));
  if (!entry) { errno = ENOMEM; return -1; }
  *entry = (exit_registration){registrations, NULL, callback, NULL, NULL};
  registrations = entry;
  return 0;
}

void __cxa_finalize(void *dso) {
  for (;;) {
    exit_registration **link = &registrations;
    while (*link && dso && (*link)->dso != dso) link = &(*link)->next;
    if (!*link) return;
    exit_registration entry = **link;
    exit_registration *removed = *link;
    *link = entry.next;
    free(removed);
    /* Retire before calling: recursive finalization cannot repeat this entry.
       Start a fresh search afterwards, including registrations by callbacks. */
    if (entry.destructor) entry.destructor(entry.argument);
    else entry.callback();
  }
}

_Noreturn void exit(int status) {
  __cxa_finalize(NULL);
  fflush(NULL);
  _Exit(status);
}
