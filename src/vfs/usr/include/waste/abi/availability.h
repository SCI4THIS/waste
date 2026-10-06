#ifndef WASTE_ABI_AVAILABILITY_H
#define WASTE_ABI_AVAILABILITY_H
/* An absent provider is a compile error, not a promise of future linkage.
 * Only the named legacy package profiles expose their own declarations. */
#ifdef WASTE_LEGACY_DECLARATIONS
#define WASTE_UNAVAILABLE(reason)
#else
#define WASTE_UNAVAILABLE(reason) __attribute__((unavailable(reason)))
#endif
#endif
