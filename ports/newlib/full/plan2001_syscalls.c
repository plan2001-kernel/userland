/* Plan2001 native AMD64 syscall adapters for genuine Newlib libc.a.
 *
 * No Plan9 libc references: the only OS entry points below are the kernel's
 * generated user syscall stubs from rt_stubs.o. Newlib supplies errno,
 * strings, malloc and stdio. This target is single-threaded.
 *
 * fd 0: explicit authorized, RO capability-region input ring.
 * fd 1,2: native authorized console objects, not inherited POSIX fds.
 * Other descriptors/paths deliberately return EBADF or ENOSYS.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include "obj.h"
#include "sysctl.h"
#include "../../unix/uxpipe.h"

extern long objcreate(unsigned long long, unsigned long long, void *,
                      unsigned long long, void *);
extern long objopen(unsigned long long, void *, long, unsigned long long,
                    void *);
extern long objinvoke(int, void *, long);
extern long objclose(int);
extern long regionwait(unsigned long long, unsigned long long,
                       unsigned long long, long long);
extern long regionwake(unsigned long long, unsigned long long,
                       unsigned long long);
extern long brk_(void *);
extern unsigned long long _nsec(void);
extern void _exits(char *);
extern char end[];

/* Newlib and the Unix supervisor use the SAME two-region UXPIPE wire format.
 * The kernel enforces RO/RW map rights. Handles do not turn into Unix FDs.
 * Binding is opt-in and does not alter standalone Newlib console probes. */
static UxPipe proxy[3];
static uintptr_t proxy_maps[6];
static int proxy_bound;
static unsigned char proxy_closed[3];
static uintptr_t read_base;
static uint64_t read_cursor;
static unsigned read_pos, read_len;
static unsigned char read_cache[OBJ_RING_DATA];
static int console_handles[2] = {-1, -1};
static unsigned char console_closed[2];

static long invoke(int handle, unsigned op, void *in, uint64_t inlen,
                   void *out, uint64_t outlen, uint64_t arg0, uint64_t arg1)
{
    ObjCall c;
    long r;
    memset(&c, 0, sizeof c);
    c.version = OBJ_ABI_VERSION;
    c.op = op;
    c.in = in;
    c.inlen = inlen;
    c.out = out;
    c.outlen = outlen;
    c.arg0 = arg0;
    c.arg1 = arg1;
    r = objinvoke(handle, &c, sizeof c);
    return r;
}

long p9_full_region_create(uint64_t n)
{
    return objcreate(0, ObjKindRegion, 0, n, 0);
}

long p9_full_region_map(int handle, int readonly)
{
    return invoke(handle, ObjOpRegionMap, 0, 0, 0, 0, 0,
                  readonly ? ObjRegionMapReadonly : 0);
}

long p9_full_region_unmap(uintptr_t base)
{
    return invoke(0, ObjOpRegionUnmap, 0, 0, 0, 0, base, 0);
}

long p9_full_region_close(int handle)
{
    return objclose(handle);
}


long region_wait(unsigned long long base, unsigned long long off,
                 unsigned long long expected, long long timeout_ms)
{
    return regionwait(base, off, expected, timeout_ms);
}
long region_wake(unsigned long long base, unsigned long long off,
                 unsigned long long count)
{
    return regionwake(base, off, count);
}
void p9_full_unbind_uxpipe(void)
{
    unsigned i;
    for(i = 0; i < 6; ++i) {
        if(proxy_maps[i]) {
            (void)p9_full_region_unmap(proxy_maps[i]);
            proxy_maps[i] = 0;
        }
    }
    memset(proxy, 0, sizeof proxy);
    memset(proxy_closed, 0, sizeof proxy_closed);
    proxy_bound = 0;
}
int p9_full_bind_uxpipe(const int handles[6])
{
    unsigned i;
    if(!handles) { errno = EINVAL; return -1; }
    if(proxy_bound || read_base || console_closed[0] || console_closed[1]) {
        errno = EBUSY; return -1;
    }
    for(i = 0; i < 6; ++i) {
        long mapped;
        int writable = (i == 1 || i == 2 || i == 4);
        if(handles[i] <= 0) goto denied;
        mapped = p9_full_region_map(handles[i], !writable);
        if(mapped <= 0) goto denied;
        proxy_maps[i] = (uintptr_t)mapped;
    }
    for(i = 0; i < 3; ++i) {
        int role = i == 0 ? UXPIPE_READER : UXPIPE_WRITER;
        if(uxpipe_bind(&proxy[i], (UxPipeData *)proxy_maps[2*i],
                       (UxPipeAck *)proxy_maps[2*i+1],
                       proxy_maps[2*i], proxy_maps[2*i+1], role))
            goto denied;
    }
    proxy_bound = 1;
    return 0;
denied:
    p9_full_unbind_uxpipe();
    errno = EACCES;
    return -1;
}

int p9_full_bind_stdin(int region_handle)
{
    long mapped;
    ObjRingHeader *header;
    if(read_base != 0 || proxy_bound) { errno = EBUSY; return -1; }
    if(region_handle <= 0) { errno = EBADF; return -1; }
    mapped = p9_full_region_map(region_handle, 1);
    if(mapped <= 0) { errno = EACCES; return -1; }
    header = (ObjRingHeader *)(uintptr_t)mapped;
    if(header->version != OBJ_RING_VERSION ||
       header->magic != OBJ_RING_MAGIC ||
       header->slots != OBJ_RING_SLOTS ||
       header->slot_size != OBJ_RING_SLOT) {
        (void)p9_full_region_unmap((uintptr_t)mapped);
        errno = EINVAL;
        return -1;
    }
    read_base = (uintptr_t)mapped;
    read_cursor = 0;
    read_pos = read_len = 0;
    return 0;
}

void p9_full_unbind_stdin(void)
{
    if(read_base != 0) {
        (void)p9_full_region_unmap(read_base);
        read_base = 0;
        read_cursor = 0;
        read_pos = read_len = 0;
    }
}

/* Newlib's reentrant read wrapper calls this platform primitive. */
int _read(int fd, char *buf, int count)
{
    ObjRingHeader *h;
    unsigned n;
    if(fd != 0 || (read_base == 0 && !proxy_bound) || proxy_closed[0]) { errno = EBADF; return -1; }
    if(count < 0) { errno = EINVAL; return -1; }
    if(count == 0) return 0;
    if(buf == 0) { errno = EFAULT; return -1; }
    if(proxy_bound) {
        long n = uxpipe_read(&proxy[0], buf,
                             (unsigned)count > UXPIPE_MAX_IO ? UXPIPE_MAX_IO : (unsigned)count, -1);
        if(n < 0) { errno = EIO; return -1; }
        return (int)n;
    }
    h = (ObjRingHeader *)read_base;
    while(read_pos == read_len) {
        uint64_t published = __atomic_load_n(&h->write_seq, __ATOMIC_ACQUIRE);
        uint64_t seq, off, marker;
        ObjRingSlot *slot;
        long result;
        if(published <= read_cursor) {
            result = regionwait(read_base, OBJ_RING_PUBOFF, published, -1);
            if(result == 3) return 0; /* producer released */
            if(result < 0) { errno = EIO; return -1; }
            continue;
        }
        if(published - read_cursor > OBJ_RING_SLOTS) {
            errno = EOVERFLOW;
            return -1;
        }
        seq = read_cursor + 1;
        off = OBJ_RING_HEADER + ((seq - 1) % OBJ_RING_SLOTS) * OBJ_RING_SLOT;
        slot = (ObjRingSlot *)(read_base + off);
        marker = __atomic_load_n(&slot->seq, __ATOMIC_ACQUIRE);
        if(marker != 2*seq || slot->length > OBJ_RING_DATA) {
            errno = EIO;
            return -1;
        }
        read_len = slot->length;
        if(read_len != 0)
            memcpy(read_cache, (void *)(read_base + off + 16), read_len);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if(__atomic_load_n(&slot->seq, __ATOMIC_ACQUIRE) != marker) {
            errno = EOVERFLOW;
            return -1;
        }
        read_pos = 0;
        read_cursor = seq;
    }
    n = read_len - read_pos;
    if(n > (unsigned)count) n = (unsigned)count;
    memcpy(buf, read_cache + read_pos, n);
    read_pos += n;
    return (int)n;
}

int _write(int fd, const char *buf, int count)
{
    ObjOpenConsoleReq req;
    unsigned sent = 0;
    long h, ret;
    int index;
    if(fd != 1 && fd != 2) { errno = EBADF; return -1; }
    if(count < 0) { errno = EINVAL; return -1; }
    if(count == 0) return 0;
    if(buf == 0) { errno = EFAULT; return -1; }
    index = fd - 1;
    if(proxy_bound) {
        unsigned wrote = 0;
        if(proxy_closed[fd]) { errno = EBADF; return -1; }
        while(wrote < (unsigned)count) {
            unsigned n = (unsigned)count - wrote;
            long got;
            if(n > UXPIPE_MAX_IO) n = UXPIPE_MAX_IO;
            got = uxpipe_write(&proxy[fd], buf + wrote, n, -1);
            if(got <= 0 || (unsigned long)got > n) {
                if(wrote) return (int)wrote;
                errno = EIO; return -1;
            }
            wrote += (unsigned)got;
        }
        return (int)wrote;
    }
    if(console_closed[index]) { errno = EBADF; return -1; }
    h = console_handles[index];
    if(h < 0) {
        req.stream = fd;
        req.reserved = 0;
        h = objopen(ObjOpenConsole, &req, sizeof req, 0, 0);
        if(h < 0) { errno = EACCES; return -1; }
        console_handles[index] = (int)h;
    }
    while(sent < (unsigned)count) {
        unsigned n = (unsigned)count - sent;
        if(n > OBJ_STREAM_MAXIO) n = OBJ_STREAM_MAXIO;
        ret = invoke((int)h, ObjOpWrite, (void *)(buf + sent), n,
                     0, 0, 0, 0);
        if(ret <= 0 || (unsigned long)ret > n) {
            if(sent) return (int)sent;
            errno = EIO;
            return -1;
        }
        sent += (unsigned)ret;
    }
    return (int)sent;
}

void *_sbrk(ptrdiff_t increment)
{
    /* Exactly one break, owned by Newlib (no Plan9 libc allocator).
     * Keep the Plan2001 legacy 8-byte BRK_ rounding.
     */
    static uintptr_t current;
    uintptr_t previous, next, inc;
    if(current == 0) {
        current = ((uintptr_t)end + 7u) & ~(uintptr_t)7u;
    }
    if(increment < 0) { errno = EINVAL; return (void *)-1; }
    previous = (current + 7u) & ~(uintptr_t)7u;
    inc = (uintptr_t)increment;
    if(previous > UINTPTR_MAX - inc) {
        errno = ENOMEM;
        return (void *)-1;
    }
    next = previous + inc;
    if(next > UINTPTR_MAX - 7u ||
       brk_((void *)((next + 7u) & ~(uintptr_t)7u)) < 0) {
        errno = ENOMEM;
        return (void *)-1;
    }
    current = next;
    return (void *)previous;
}

_Noreturn void _exit(int code)
{
    /* EXITS preserves success/failure, not the exact numeric Unix code. */
    _exits(code == 0 ? "" : "error");
    __builtin_unreachable();
}

int _close(int fd)
{
    if(proxy_bound && fd >= 0 && fd <= 2) {
        if(proxy_closed[fd]) { errno = EBADF; return -1; }
        proxy_closed[fd] = 1;
        if(uxpipe_finish(&proxy[fd])) { errno = EIO; return -1; }
        return 0;
    }
    if(fd == 0 && read_base != 0) { p9_full_unbind_stdin(); return 0; }
    if(fd == 1 || fd == 2) {
        int index = fd - 1;
        if(console_closed[index]) { errno = EBADF; return -1; }
        console_closed[index] = 1;
        if(console_handles[index] >= 0)
            return objclose(console_handles[index]) < 0
                ? (errno = EIO, -1) : 0;
        return 0;
    }
    errno = EBADF;
    return -1;
}

int _fstat(int fd, struct stat *st)
{
    if(st == 0) { errno = EFAULT; return -1; }
    if(proxy_bound && fd >= 0 && fd <= 2 && !proxy_closed[fd]) {
        memset(st, 0, sizeof *st);
        st->st_mode = S_IFIFO | 0600;
        st->st_nlink = 1;
        st->st_blksize = UXPIPE_CAPACITY;
        return 0;
    }
    if((fd == 0 && read_base != 0) ||
       (fd >= 1 && fd <= 2 && !console_closed[fd-1])) {
        memset(st, 0, sizeof *st);
        st->st_mode = S_IFCHR | 0600;
        st->st_nlink = 1;
        st->st_blksize = 1;
        return 0;
    }
    errno = EBADF;
    return -1;
}

int _isatty(int fd)
{
    if(proxy_bound && fd >= 0 && fd <= 2) {
        errno = proxy_closed[fd] ? EBADF : ENOTTY;
        return 0;
    }
    if(fd == 1 || fd == 2) {
        if(!console_closed[fd-1]) return 1;
    } else if(fd == 0 && read_base != 0) {
        return 1;
    }
    errno = EBADF;
    return 0;
}

off_t _lseek(int fd, off_t offset, int whence)
{
    (void)offset; (void)whence;
    if(proxy_bound && fd >= 0 && fd <= 2) {
        errno = proxy_closed[fd] ? EBADF : ESPIPE;
        return (off_t)-1;
    }
    if((fd >= 1 && fd <= 2 && !console_closed[fd-1]) ||
       (fd == 0 && read_base != 0)) {
        errno = ESPIPE;
    } else {
        errno = EBADF;
    }
    return (off_t)-1;
}

int _getpid(void)
{
    SysctlReq req;
    SysctlProcInfo info;
    memset(&req, 0, sizeof req);
    memset(&info, 0, sizeof info);
    req.version = SYSCTL_ABI_VERSION;
    req.data = &info;
    req.datalen = sizeof info;
    if(sysctl(SysctlScopeOwnProcess, SysctlProcessInfo, &req, sizeof req) < 0) {
        errno = EIO;
        return -1;
    }
    return (int)info.pid;
}

int _kill(int pid, int sig)
{
    (void)pid; (void)sig;
    errno = ENOSYS; /* PID does not grant process-control authority */
    return -1;
}

int _open(const char *path, int flags, ...)
{
    (void)path; (void)flags;
    errno = ENOSYS; /* no ambient pathname namespace */
    return -1;
}
int _unlink(const char *path)
{
    (void)path; errno = ENOSYS; return -1;
}
int _link(const char *existing, const char *newpath)
{
    (void)existing; (void)newpath; errno = ENOSYS; return -1;
}
int _stat(const char *path, struct stat *st)
{
    (void)path; (void)st; errno = ENOSYS; return -1;
}
int _fork(void)
{
    errno = ENOSYS; return -1; /* native RFORKEX is not POSIX fork */
}
int _execve(const char *path, char *const argv[], char *const envp[])
{
    (void)path; (void)argv; (void)envp;
    errno = ENOSYS; return -1;
}
int _wait(int *status)
{
    (void)status; errno = ENOSYS; return -1;
}
clock_t _times(struct tms *t)
{
    (void)t; errno = ENOSYS; return (clock_t)-1;
}
int _gettimeofday(struct timeval *tv, void *tz)
{
    unsigned long long ns;
    if(tv == 0) { errno = EFAULT; return -1; }
    ns = _nsec(); /* Direct native _NSEC(53), UTC nanoseconds since epoch. */
    tv->tv_sec = (time_t)(ns / 1000000000ULL);
    tv->tv_usec = (suseconds_t)((ns % 1000000000ULL) / 1000ULL);
    if(tz != 0) {
        /* Legacy timezone output: UTC; no per-process timezone service. */
        struct timezone *zone = (struct timezone *)tz;
        zone->tz_minuteswest = 0;
        zone->tz_dsttime = 0;
    }
    return 0;
}
