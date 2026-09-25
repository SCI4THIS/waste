#ifndef WASTE_NETDB_H
#define WASTE_NETDB_H

struct addrinfo {
  int ai_flags;
  int ai_family;
  int ai_socktype;
  int ai_protocol;
  unsigned int ai_addrlen;
  void *ai_addr;
  char *ai_canonname;
  struct addrinfo *ai_next;
};

#define AI_CANONNAME 0x0002
#define EAI_BADFLAGS (-1)
#define EAI_NONAME (-2)
#define EAI_AGAIN (-3)
#define EAI_FAIL (-4)
#define EAI_MEMORY (-10)

int getaddrinfo(const char *, const char *, const struct addrinfo *,
                struct addrinfo **);
void freeaddrinfo(struct addrinfo *);
char *gai_strerror(int);

#endif
