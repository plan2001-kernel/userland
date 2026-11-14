/* Newlib 4.6.0 _r wrappers on the AMD64 unknown-elf target are built
 * against public POSIX symbol names when supplied syscalls are disabled.
 * Forward these to the Plan2001 native underscore primitives. These
 * entrypoints are REAL, not fake-success stubs.
 *
 * errno is supplied by upstream Newlib's reentrancy runtime.
 */
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

extern int _read(int, char *, int);
extern int _write(int, const char *, int);
extern int _close(int);
extern off_t _lseek(int, off_t, int);
extern void *_sbrk(ptrdiff_t);
extern int _fstat(int, struct stat *);
extern int _isatty(int);
extern int _getpid(void);
extern int _kill(int, int);

int read(int fd, void *buf, size_t count)
{
    if(count > INT_MAX) { errno = EINVAL; return -1; }
    return _read(fd, (char *)buf, (int)count);
}

int write(int fd, const void *buf, size_t count)
{
    if(count > INT_MAX) { errno = EINVAL; return -1; }
    return _write(fd, (const char *)buf, (int)count);
}

int close(int fd)
{
    return _close(fd);
}

off_t lseek(int fd, off_t offset, int whence)
{
    return _lseek(fd, offset, whence);
}

void *sbrk(ptrdiff_t change)
{
    return _sbrk(change);
}

int fstat(int fd, struct stat *st)
{
    return _fstat(fd, st);
}

int isatty(int fd)
{
    return _isatty(fd);
}

pid_t getpid(void)
{
    return (pid_t)_getpid();
}

int kill(pid_t pid, int sig)
{
    return _kill((int)pid, sig);
}
