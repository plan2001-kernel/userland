/* Real-kernel QEMU smoke for native Newlib/libgloss four-function bridge. */
#include "libgloss_plan2001.h"
#include "obj.h"

extern long obj_region_create(unsigned long long, ObjId *);
extern long obj_region_map(int, unsigned long long);
extern long obj_region_unmap(unsigned long long);
extern long obj_close(int);
extern void *memmove(void *, const void *, unsigned long);
extern int memcmp(const void *, const void *, unsigned long);
extern long sleep(long);
extern void _exits(char *);

int errno; /* Isolated probe only. Full Newlib provides errno state. */

static void emit(const char *msg, int len)
{
    if(_write(1, msg, len) != len)
        _exits("libgloss stdout failed");
}
static void fail(void)
{
    static const char m[] = "PLAN2001-LIBGLOSS-GUEST-FAIL\n";
    emit(m, sizeof(m)-1);
    _exits("libgloss smoke failed");
}
#define ASSERT(x) do { if(!(x)) fail(); } while(0)
#define MARK(t) do { static const char m[] = t "\n"; emit(m, sizeof(m)-1); } while(0)

int main(void)
{
    long region, write_map;
    ObjRingHeader *hdr;
    ObjRingSlot *slot;
    char buf[16];
    unsigned char *data;
    unsigned char *p0, *p1, *p2;
    static const char payload[] = "abcdefg";

    errno = 0;
    ASSERT(_read(0, buf, sizeof(buf)) == -1 && errno == P9_EBADF);
    errno = 0;
    ASSERT(_write(0, "x", 1) == -1 && errno == P9_EBADF);
    errno = 0;
    ASSERT(_write(1, 0, 1) == -1 && errno == P9_EFAULT);

    region = obj_region_create(OBJ_RING_BYTES, 0);
    ASSERT(region > 0);
    write_map = obj_region_map((int)region, 0);
    ASSERT(write_map > 0);
    hdr = (ObjRingHeader *)(unsigned long)write_map;
    hdr->version = OBJ_RING_VERSION;
    hdr->magic = OBJ_RING_MAGIC;
    hdr->slots = OBJ_RING_SLOTS;
    hdr->slot_size = OBJ_RING_SLOT;
    slot = (ObjRingSlot *)(unsigned long)(write_map + OBJ_RING_HEADER);
    data = (unsigned char *)(slot + 1);
    __atomic_store_n(&slot->seq, 1, __ATOMIC_RELAXED);
    slot->length = sizeof(payload)-1;
    slot->flags = 0;
    memmove(data, payload, sizeof(payload)-1);
    __atomic_store_n(&slot->seq, 2, __ATOMIC_RELEASE);
    __atomic_store_n(&hdr->write_seq, 1, __ATOMIC_RELEASE);

    ASSERT(p9_gloss_bind_stdin((int)region) == 0);
    ASSERT(_read(0, buf, 3) == 3 && memcmp(buf, "abc", 3) == 0);
    ASSERT(_read(0, buf, 4) == 4 && memcmp(buf, "defg", 4) == 0);
    ASSERT(_read(0, buf, 0) == 0);
    p9_gloss_unbind_stdin();
    ASSERT(_read(0, buf, 1) == -1 && errno == P9_EBADF);
    ASSERT(obj_region_unmap((unsigned long long)write_map) == 0);
    ASSERT(obj_close((int)region) == 0);
    MARK("PLAN2001-LIBGLOSS-READ-PASS");

    p0 = (unsigned char *)_sbrk(0);
    ASSERT(p0 != (void *)-1);
    p1 = (unsigned char *)_sbrk(32);
    ASSERT(p1 == p0);
    p1[0] = 0x5a;
    p1[31] = 0xa5;
    p2 = (unsigned char *)_sbrk(0);
    ASSERT(p2 == p1 + 32 && p1[0] == 0x5a && p1[31] == 0xa5);
    errno = 0;
    ASSERT(_sbrk(-1) == (void *)-1 && errno == P9_EINVAL);
    ASSERT(_sbrk(0) == p2);
    MARK("PLAN2001-LIBGLOSS-SBRK-PASS");

    MARK("PLAN2001-LIBGLOSS-WRITE-PASS");
    MARK("PLAN2001-LIBGLOSS-GUEST-PASS");
    /* Parent boot process is kept alive after the checked markers. _exit
     * itself is tested separately via ordinary spawned child processes. */
    for (;;)
        (void)sleep(1000);
}
