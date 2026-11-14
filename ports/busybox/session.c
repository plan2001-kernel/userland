/* Kernel-owned boot session console over native capability-region rings.
 * This adapter never opens a physical keyboard, framebuffer or guessed OID.
 * Only an EXACT inherited stdin RO + stdout RW + diagnostics RW grant triple
 * is treated as a session; SPAWN's legacy single output region is NOT one.
 */
#include "libbb.h"
#include "obj.h"
#include "vobj.h"

extern long obj_list(void *, unsigned long long);
extern long obj_region_map(int, unsigned long long);
extern long obj_region_unmap(unsigned long long);
extern long region_wait(unsigned long long, unsigned long long,
                        unsigned long long, long long);
extern long region_wake(unsigned long long, unsigned long long,
                        unsigned long long);

static int probe_state;                 /* 0 unprobed, 1 session, -1 absent, -2 broken */
static unsigned long long inbase, outbase, diagbase;
static unsigned long long inlast;
static unsigned char inbuf[OBJ_RING_DATA];
static unsigned int inpos, inlen;

static unsigned long long acquire(const unsigned long long *p)
{
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static void release(unsigned long long *p, unsigned long long n)
{
    __atomic_store_n(p, n, __ATOMIC_RELEASE);
}

static int ring_valid(unsigned long long base)
{
    const ObjRingHeader *h = (const ObjRingHeader *)base;
    return h->version == OBJ_RING_VERSION &&
           h->magic == OBJ_RING_MAGIC &&
           h->slot_size == OBJ_RING_SLOT &&
           h->slots == OBJ_RING_SLOTS;
}

int p2001_session_probe(void)
{
    VobjListRec handles[32];
    long count, i;
    int inh = -1, outh = -1, diagh = -1;
    long ib = -1, ob = -1, db = -1;

    if (probe_state)
        return probe_state == 1 ? 1 : probe_state == -1 ? 0 : -1;
    count = obj_list(handles, 32);
    if (count < 0 || count > 32) {
        probe_state = -2;
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if (handles[i].kind != VkindRegion)
            continue;
        if ((handles[i].rights & Vwrite) != 0) {
            if (outh < 0)
                outh = (int)handles[i].handle;
            else if (diagh < 0)
                diagh = (int)handles[i].handle;
            else
                goto broken;
        } else if ((handles[i].rights & Vread) != 0) {
            if (inh >= 0)
                goto broken;
            inh = (int)handles[i].handle;
        }
    }
    /* Native SPAWN fixtures may receive one RO file-view region and
     * one RW output region. Neither is a kernel console session. The
     * exact three-grant session contract is mandatory for console access.
     */
    if (diagh < 0) {
        probe_state = -1;
        return 0;
    }
    if (inh < 0 || outh < 0 || diagh < 0)
        goto broken;

    ib = obj_region_map(inh, 0);
    if (ib < 0)
        goto broken;
    ob = obj_region_map(outh, 0);
    if (ob < 0)
        goto broken;
    db = obj_region_map(diagh, 0);
    if (db < 0)
        goto broken;
    if (!ring_valid((unsigned long long)ib) ||
        !ring_valid((unsigned long long)ob) ||
        !ring_valid((unsigned long long)db))
        goto broken;
    inbase = (unsigned long long)ib;
    outbase = (unsigned long long)ob;
    diagbase = (unsigned long long)db;
    inlast = acquire(&((ObjRingHeader *)inbase)->write_seq);
    inpos = inlen = 0;
    probe_state = 1;
    return 1;

broken:
    if (db >= 0)
        (void)obj_region_unmap((unsigned long long)db);
    if (ob >= 0)
        (void)obj_region_unmap((unsigned long long)ob);
    if (ib >= 0)
        (void)obj_region_unmap((unsigned long long)ib);
    probe_state = -2;
    return -1;
}

/* Bounded ring with backpressure from the kernel's ACK sequence.
 * A write is successful only after its bytes have been published. */
ssize_t p2001_session_write(int fd, const void *buf, size_t count)
{
    const unsigned char *p = (const unsigned char *)buf;
    unsigned long long base, seq, ack, off;
    ObjRingHeader *h;
    size_t done = 0;
    unsigned int n;
    long status;

    if (p2001_session_probe() != 1 || (fd != 1 && fd != 2))
        return -1;
    base = fd == 1 ? outbase : diagbase;
    h = (ObjRingHeader *)base;
    while (done < count) {
        seq = acquire(&h->write_seq);
        ack = acquire(&h->ack_seq);
        while (seq - ack >= OBJ_RING_SLOTS) {
            status = region_wait(base, OBJ_RING_ACKOFF, ack, -1);
            if (status < 0 || status == 3)
                return done ? (ssize_t)done : -1;
            ack = acquire(&h->ack_seq);
        }
        n = count - done > OBJ_RING_DATA ?
            OBJ_RING_DATA : (unsigned int)(count - done);
        off = OBJ_RING_HEADER + (seq % OBJ_RING_SLOTS) * OBJ_RING_SLOT;
        release((unsigned long long *)(base + off), 2*(seq+1)-1);
        *(volatile unsigned int *)(base + off + 8) = n;
        *(volatile unsigned int *)(base + off + 12) = fd == 2 ? 1u : 0u;
        for (unsigned int j=0; j<n; ++j)
            *(volatile unsigned char *)(base + off + 16 + j) = p[done+j];
        __atomic_thread_fence(__ATOMIC_RELEASE);
        release((unsigned long long *)(base + off), 2*(seq+1));
        release(&h->write_seq, seq+1);
        (void)region_wake(base, OBJ_RING_PUBOFF, 1);
        done += n;
    }
    return (ssize_t)done;
}

/* Raw console byte input, not yet POSIX canonical/termios semantics.
 * Avoid consuming unpublished or overwritten slots. */
ssize_t p2001_session_read(int fd, void *buf, size_t count)
{
    ObjRingHeader *h;
    unsigned long long seq, off;
    unsigned int len;
    long status;
    unsigned char *p = (unsigned char *)buf;
    size_t n;

    if (fd != 0 || p2001_session_probe() != 1)
        return -1;
    if (!count)
        return 0;
    h = (ObjRingHeader *)inbase;
    for (;;) {
        if (inpos < inlen) {
            n = count < (size_t)(inlen - inpos) ?
                count : (size_t)(inlen - inpos);
            for (size_t j=0; j<n; ++j)
                p[j] = inbuf[inpos+j];
            inpos += (unsigned int)n;
            return (ssize_t)n;
        }
        seq = acquire(&h->write_seq);
        if (seq - inlast > OBJ_RING_SLOTS)
            inlast = seq - OBJ_RING_SLOTS;
        while (inlast < seq) {
            ++inlast;
            off = OBJ_RING_HEADER +
                  ((inlast-1) % OBJ_RING_SLOTS) * OBJ_RING_SLOT;
            if (acquire((unsigned long long *)(inbase+off)) != 2*inlast)
                continue;
            len = *(volatile unsigned int *)(inbase+off+8);
            if (len == 0 || len > OBJ_RING_DATA)
                continue;
            for (unsigned int j=0; j<len; ++j)
                inbuf[j] = *(volatile unsigned char *)(inbase+off+16+j);
            if (acquire((unsigned long long *)(inbase+off)) != 2*inlast)
                continue;
            inpos = 0;
            inlen = len;
            break;
        }
        if (inpos < inlen)
            continue;
        status = region_wait(inbase, OBJ_RING_PUBOFF, inlast, -1);
        if (status == 3)
            return 0;
        if (status < 0)
            return -1;
    }
}
