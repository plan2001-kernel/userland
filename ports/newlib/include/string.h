/* Trial-only declarations to compile unmodified Newlib string sources. */
#ifndef PLAN2001_NEWLIB_TRIAL_STRING
#define PLAN2001_NEWLIB_TRIAL_STRING
#include <sys/types.h>
size_t strlen(const char *);
size_t strlcpy(char *, const char *, size_t);
size_t strlcat(char *, const char *, size_t);
size_t strnlen(const char *, size_t);
#endif
