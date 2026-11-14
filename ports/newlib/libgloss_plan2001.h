/* Minimal experimental libgloss/Newlib target ABI.
 * These numbers follow the Newlib AMD64 errno definitions used in the
 * isolated probe; full Newlib integration will use its real <errno.h>.
 */
#ifndef PLAN2001_NEWLIB_LIBGLOSS_H
#define PLAN2001_NEWLIB_LIBGLOSS_H

typedef __PTRDIFF_TYPE__ p9_ptrdiff_t;

enum {
    P9_EIO = 5,
    P9_EBADF = 9,
    P9_ENOMEM = 12,
    P9_EACCES = 13,
    P9_EFAULT = 14,
    P9_EBUSY = 16,
    P9_EINVAL = 22,
    P9_EOVERFLOW = 75
};

int p9_gloss_bind_stdin(int region);
void p9_gloss_unbind_stdin(void);
int _read(int fd, char *buffer, int count);
int _write(int fd, const char *buffer, int count);
void *_sbrk(p9_ptrdiff_t increment);
_Noreturn void _exit(int code);

#endif
