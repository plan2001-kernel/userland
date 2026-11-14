/* Experimental Newlib/libgloss system call boundary for Plan2001 AMD64.
 *
 * _read: explicitly bound read-only capability-region input ring, fd=0.
 * _write: native authorized ObjOpenConsole/ObjOpWrite, fd=1 or fd=2.
 * _sbrk: share the existing native libc's BRK_-backed sbrk break, never
 *        maintain a second independent heap pointer.
 * _exit: direct native EXITS (success/failed, not numerical POSIX status).
 *
 * This is NOT a complete Newlib port: no POSIX paths/FD table/TTY, fd0 is
 * not ambient; a session provider must explicitly grant the region.
 * No kernel changes or read/write legacy syscall numbers are used.
 */
#include "libgloss_plan2001.h"
#include "obj.h"

extern int errno;
extern long obj_open(unsigned long long, void *, long, unsigned long long, ObjId *);
extern long obj_write(int, void *, unsigned long long, unsigned long long);
extern long obj_region_map_flags(int, unsigned long long, unsigned int);
extern long obj_region_unmap(unsigned long long);
extern long region_wait(unsigned long long, unsigned long long,
                        unsigned long long, long long);
extern long obj_close(int);
extern void *sbrk(unsigned long);
extern void _exits(char *);
extern void *memmove(void *, const void *, unsigned long);

static unsigned long long input_base;
static unsigned long long input_last;
static unsigned int input_pos, input_len;
static unsigned char input_copy[OBJ_RING_DATA];
static int stdout_handle = -1, stderr_handle = -1;

static int fail(int err)
{
    errno = err;
    return -1;
}

/* Caller must own an authorized readable region Handle containing the native
 * console/session ObjRingHeader. A read-only mapping is installed, even if
 * caller also has Vwrite; guessing a region address never grants authority.
 * No implicit Handle discovery or ambient stdin is accepted.
 */
int
p9_gloss_bind_stdin(int region)
{
    unsigned long long base;
    ObjRingHeader *h;

    if(input_base != 0)
        return fail(P9_EBUSY);
    if(region <= 0)
        return fail(P9_EBADF);
    base = (unsigned long long)obj_region_map_flags(
        region, 0, ObjRegionMapReadonly);
    if((long)base < 0)
        return fail(P9_EACCES);
    h = (ObjRingHeader *)(unsigned long)base;
    if(h->version != OBJ_RING_VERSION || h->magic != OBJ_RING_MAGIC ||
       h->slot_size != OBJ_RING_SLOT || h->slots != OBJ_RING_SLOTS) {
        (void)obj_region_unmap(base);
        return fail(P9_EINVAL);
    }
    input_last = 0;
    input_len = input_pos = 0;
    input_base = base;
    return 0;
}

void
p9_gloss_unbind_stdin(void)
{
    if(input_base != 0) {
        (void)obj_region_unmap(input_base);
        input_base = 0;
        input_pos = input_len = 0;
        input_last = 0;
    }
}

int
_read(int fd, char *dst, int count)
{
    unsigned int want, copylen;
    ObjRingHeader *h;

    if(fd != 0 || input_base == 0)
        return fail(P9_EBADF);
    if(count < 0)
        return fail(P9_EINVAL);
    if(count == 0)
        return 0;
    if(dst == 0)
        return fail(P9_EFAULT);

    want = (unsigned int)count;
    h = (ObjRingHeader *)(unsigned long)input_base;
    while(input_pos == input_len) {
        unsigned long long published = __atomic_load_n(
            &h->write_seq, __ATOMIC_ACQUIRE);
        unsigned long long seq, off, marker;
        ObjRingSlot *slot;
        if(published <= input_last) {
            long r = region_wait(input_base, OBJ_RING_PUBOFF,
                                 published, -1);
            if(r == 3) return 0; /* producer mapping gone: EOF */
            if(r < 0) return fail(P9_EIO);
            continue;
        }
        if(published - input_last > OBJ_RING_SLOTS)
            return fail(P9_EOVERFLOW); /* no silent loss of overwritten input */
        seq = input_last + 1;
        off = OBJ_RING_HEADER + ((seq - 1) % OBJ_RING_SLOTS)*OBJ_RING_SLOT;
        slot = (ObjRingSlot *)(unsigned long)(input_base + off);
        marker = __atomic_load_n(&slot->seq, __ATOMIC_ACQUIRE);
        if(marker != 2*seq || slot->length > OBJ_RING_DATA)
            return fail(P9_EIO);
        input_len = slot->length;
        if(input_len != 0)
            memmove(input_copy, (void *)(unsigned long)(input_base + off + 16),
                    input_len);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if(__atomic_load_n(&slot->seq, __ATOMIC_ACQUIRE) != marker)
            return fail(P9_EOVERFLOW);
        input_pos = 0;
        input_last = seq;
    }
    copylen = input_len - input_pos;
    if(copylen > want)
        copylen = want;
    memmove(dst, input_copy + input_pos, copylen);
    input_pos += copylen;
    return (int)copylen;
}

/* Open exactly the authorized process stream. A spawned process without
 * iomask cannot acquire console rights: this operation must fail, not
 * conjure an inherited descriptor from a numeric Handle.
 */
int
_write(int fd, const char *buf, int count)
{
    int *cached;
    ObjOpenConsoleReq req;
    unsigned int written = 0;
    if(fd != 1 && fd != 2)
        return fail(P9_EBADF);
    if(count < 0)
        return fail(P9_EINVAL);
    if(count == 0)
        return 0;
    if(buf == 0)
        return fail(P9_EFAULT);
    cached = (fd == 1) ? &stdout_handle : &stderr_handle;
    if(*cached < 0) {
        req.stream = fd;
        req.reserved = 0;
        long h = obj_open(ObjOpenConsole, &req, sizeof(req), 0, 0);
        if(h < 0)
            return fail(P9_EACCES);
        *cached = (int)h;
    }
    while(written < (unsigned int)count) {
        unsigned int n = (unsigned int)count - written;
        long ret;
        if(n > OBJ_STREAM_MAXIO)
            n = OBJ_STREAM_MAXIO;
        ret = obj_write(*cached, (void *)(buf + written), n, 0);
        if(ret < 0) {
            if(written != 0)
                return (int)written; /* POSIX short write */
            return fail(P9_EIO);
        }
        if(ret == 0 || (unsigned long)ret > n) {
            if(written != 0)
                return (int)written;
            return fail(P9_EIO);
        }
        written += (unsigned int)ret;
    }
    return (int)written;
}

void *
_sbrk(p9_ptrdiff_t increment)
{
    void *old;
    if(increment < 0) {
        errno = P9_EINVAL; /* shrinking is not offered by this adapter */
        return (void *)-1;
    }
    old = sbrk((unsigned long)increment);
    if(old == (void *)-1)
        errno = P9_ENOMEM;
    return old;
}

_Noreturn void
_exit(int code)
{
    /* Plan2001 currently distinguishes empty (success) from nonempty
     * (failure) exit messages. Numeric status is not preserved by EXITS. */
    _exits(code == 0 ? "" : "error");
    __builtin_unreachable();
}
