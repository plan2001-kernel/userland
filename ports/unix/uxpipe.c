/* Native Plan2001 region stream: release/acquire byte cursor publication,
 * bounded overwrite prevention, explicit EOF and peer-abandonment signals.
 *
 * Producer: RW data + RO ACK. Consumer: RO data + RW ACK. The kernel's
 * unique writer mapping prevents an unauthorized process from publishing a
 * stream cursor. Wake carries no payload; each reader rechecks atomics.
 *
 * REGIONWAIT status 3 (publisher mapping disappeared) is an ERROR unless
 * EOF was explicitly published. It is not a successful POSIX EOF.
 */
#include "uxpipe.h"
extern long region_wait(unsigned long long base, unsigned long long offset,
                        unsigned long long expected, long long timeout);
extern long region_wake(unsigned long long base, unsigned long long offset,
                        unsigned long long count);

static unsigned long long load(const unsigned long long *p)
{
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
static void publish(unsigned long long *p, unsigned long long v)
{
    __atomic_store_n(p,v,__ATOMIC_RELEASE);
}
int uxpipe_format(UxPipeData *data, UxPipeAck *ack)
{
    unsigned int i;
    if (!data || !ack) return -1;
    data->magic = UXPIPE_DATA_MAGIC;
    data->version = UXPIPE_VERSION;
    for (i=0;i<UXPIPE_CAPACITY;++i) data->payload[i]=0;
    publish(&data->produced,0);
    publish(&data->closed,0);
    ack->magic = UXPIPE_ACK_MAGIC;
    ack->version = UXPIPE_VERSION;
    publish(&ack->consumed,0);
    publish(&ack->closed,0);
    return 0;
}
static int valid(UxPipe *pipe)
{
    unsigned long long wr, rd;
    if (!pipe || !pipe->data || !pipe->ack ||
        pipe->data->magic != UXPIPE_DATA_MAGIC ||
        pipe->ack->magic != UXPIPE_ACK_MAGIC ||
        pipe->data->version != UXPIPE_VERSION ||
        pipe->ack->version != UXPIPE_VERSION ||
        (pipe->role != UXPIPE_WRITER && pipe->role != UXPIPE_READER))
        return 0;
    wr = load(&pipe->data->produced);
    rd = load(&pipe->ack->consumed);
    return wr >= rd && wr-rd <= UXPIPE_CAPACITY;
}
int uxpipe_bind(UxPipe *pipe, UxPipeData *data, UxPipeAck *ack,
                unsigned long long data_base,
                unsigned long long ack_base, int role)
{
    if (!pipe || !data || !ack || !data_base || !ack_base ||
        data_base != (unsigned long long)data ||
        ack_base != (unsigned long long)ack ||
        (role != UXPIPE_WRITER && role != UXPIPE_READER)) return -1;
    pipe->data = data;
    pipe->ack = ack;
    pipe->data_base = data_base;
    pipe->ack_base = ack_base;
    pipe->role = role;
    return valid(pipe) ? 0 : -1;
}
long uxpipe_write(UxPipe *pipe, const void *bytes,
                  unsigned int count, long long timeout_ms)
{
    const unsigned char *src = (const unsigned char *)bytes;
    unsigned int done=0, n, offset, i;
    unsigned long long wr, rd;
    long rc;
    if (!pipe || pipe->role != UXPIPE_WRITER || !valid(pipe) ||
        (!src && count) || count > UXPIPE_MAX_IO ||
        timeout_ms < -1) return -1;
    while (done<count) {
        if (!valid(pipe) || load(&pipe->data->closed) ||
            load(&pipe->ack->closed))
            return done ? (long)done : -1;
        wr=load(&pipe->data->produced);
        rd=load(&pipe->ack->consumed);
        if (wr < rd || wr-rd > UXPIPE_CAPACITY)
            return done ? (long)done : -1;
        if (wr-rd == UXPIPE_CAPACITY) {
            rc=region_wait(pipe->ack_base, UXPIPE_ACK_OFFSET, rd,
                           timeout_ms);
            if (rc < 0 || rc==2 || rc==3)
                return done ? (long)done : -1;
            continue; /* wake has no payload, recheck cursor */
        }
        offset=(unsigned int)(wr % UXPIPE_CAPACITY);
        n=UXPIPE_CAPACITY-(unsigned int)(wr-rd);
        if (n>UXPIPE_CAPACITY-offset) n=UXPIPE_CAPACITY-offset;
        if (n>count-done) n=count-done;
        for (i=0;i<n;++i) pipe->data->payload[offset+i]=src[done+i];
        publish(&pipe->data->produced,wr+n);
        (void)region_wake(pipe->data_base, UXPIPE_PUB_OFFSET,1);
        done+=n;
    }
    return (long)done;
}
long uxpipe_read(UxPipe *pipe, void *bytes,
                 unsigned int capacity, long long timeout_ms)
{
    unsigned char *dst=(unsigned char *)bytes;
    unsigned long long wr, rd;
    unsigned int n, offset, i;
    long rc;
    if (!pipe || pipe->role != UXPIPE_READER || !valid(pipe) ||
        (!dst && capacity) || capacity > UXPIPE_MAX_IO ||
        timeout_ms < -1) return -1;
    if (!capacity) return 0;
    for (;;) {
        if (!valid(pipe) || load(&pipe->ack->closed)) return -1;
        wr=load(&pipe->data->produced);
        rd=load(&pipe->ack->consumed);
        if (wr<rd || wr-rd>UXPIPE_CAPACITY) return -1;
        if (wr!=rd) break;
        if (load(&pipe->data->closed)) return 0; /* explicit EOF */
        rc=region_wait(pipe->data_base, UXPIPE_PUB_OFFSET,wr,timeout_ms);
        if (rc < 0 || rc==2 || rc==3) return -1;
    }
    offset=(unsigned int)(rd % UXPIPE_CAPACITY);
    n=(unsigned int)(wr-rd);
    if (n>UXPIPE_CAPACITY-offset) n=UXPIPE_CAPACITY-offset;
    if (n>capacity) n=capacity;
    for (i=0;i<n;++i) dst[i]=pipe->data->payload[offset+i];
    publish(&pipe->ack->consumed,rd+n);
    (void)region_wake(pipe->ack_base,UXPIPE_ACK_OFFSET,1);
    return (long)n;
}
int uxpipe_finish(UxPipe *pipe)
{
    if (!valid(pipe)) return -1;
    if (pipe->role == UXPIPE_WRITER) {
        publish(&pipe->data->closed,1);
        (void)region_wake(pipe->data_base,UXPIPE_PUB_OFFSET,1);
    } else {
        publish(&pipe->ack->closed,1);
        (void)region_wake(pipe->ack_base,UXPIPE_ACK_OFFSET,1);
    }
    return 0;
}
