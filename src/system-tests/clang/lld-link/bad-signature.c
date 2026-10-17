/* A real unresolved call whose type conflicts with the shared provider. */
extern int printf(void);
int test(void) { return printf(); }
