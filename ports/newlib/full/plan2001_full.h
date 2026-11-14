#ifndef PLAN2001_NEWLIB_FULL_H
#define PLAN2001_NEWLIB_FULL_H
#include <stdint.h>
#include <stddef.h>
long p9_full_region_create(uint64_t size);
long p9_full_region_map(int handle, int readonly);
long p9_full_region_unmap(uintptr_t base);
long p9_full_region_close(int handle);
/* Explicit six-grant UXPIPE supervisor session; never guesses console rights. */
int p9_full_bind_uxpipe(const int region_handles[6]);
void p9_full_unbind_uxpipe(void);
int p9_full_bind_stdin(int region_handle);
void p9_full_unbind_stdin(void);
#endif
