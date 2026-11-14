/* Real pthread producer/consumer verifies the SAME freestanding pipe code.
 * Mock only kernel REGIONWAIT/WAKE using a condvar; each read/write cursor
 * remains native C11/GCC atomic. This does not establish guest IPC by itself.
 */
#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include "uxpipe.h"

static UxPipeData data;
static UxPipeAck ack;
static UxPipe writer, reader;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static unsigned int waits, wakes;
static int injected_gone;
long region_wait(unsigned long long base, unsigned long long offset,
                 unsigned long long expected, long long timeout)
{
    unsigned long long *word=(unsigned long long *)(base+offset);
    int rc=0;
    if (injected_gone) return 3;
    pthread_mutex_lock(&lock);
    ++waits;
    while (__atomic_load_n(word,__ATOMIC_ACQUIRE)==expected) {
        if (timeout==0) {rc=2;break;}
        if (timeout<0) {
            assert(pthread_cond_wait(&changed,&lock)==0);
        } else {
            struct timespec limit;
            assert(clock_gettime(CLOCK_REALTIME,&limit)==0);
            limit.tv_sec+=timeout/1000;
            limit.tv_nsec+=(timeout%1000)*1000000;
            if (limit.tv_nsec>=1000000000L) {
                ++limit.tv_sec;limit.tv_nsec-=1000000000L;
            }
            if (pthread_cond_timedwait(&changed,&lock,&limit)==ETIMEDOUT) {
                rc=2;break;
            }
        }
    }
    pthread_mutex_unlock(&lock);
    return rc ? rc : 1;
}
long region_wake(unsigned long long base, unsigned long long offset,
                 unsigned long long count)
{
    (void)base; (void)offset; (void)count;
    pthread_mutex_lock(&lock);
    ++wakes;
    assert(pthread_cond_broadcast(&changed)==0);
    pthread_mutex_unlock(&lock);
    return 1;
}
static void reset(void)
{
    assert(uxpipe_format(&data,&ack)==0);
    assert(uxpipe_bind(&writer,&data,&ack,(unsigned long long)&data,
                       (unsigned long long)&ack,UXPIPE_WRITER)==0);
    assert(uxpipe_bind(&reader,&data,&ack,(unsigned long long)&data,
                       (unsigned long long)&ack,UXPIPE_READER)==0);
}
static unsigned char payload[10000];
static void *producer(void *unused)
{
    (void)unused;
    assert(uxpipe_write(&writer,payload,10000,-1)==10000);
    assert(uxpipe_finish(&writer)==0);
    return 0;
}
int main(void)
{
    unsigned char received[10000], one[1], a[UXPIPE_CAPACITY];
    unsigned int i, total=0;
    long n;
    pthread_t thread;
    reset();
    assert(uxpipe_read(&reader,one,1,0)==-1); /* would block */
    assert(uxpipe_write(&writer,payload,UXPIPE_MAX_IO+1u,0)==-1);
    assert(uxpipe_write(&writer,payload,UXPIPE_CAPACITY+1u,0)==UXPIPE_CAPACITY);
    assert(uxpipe_read(&reader,a,UXPIPE_CAPACITY,0)==UXPIPE_CAPACITY);
    assert(uxpipe_write(&writer,payload+UXPIPE_CAPACITY,1,0)==1);
    assert(uxpipe_read(&reader,one,1,0)==1);
    assert(uxpipe_finish(&writer)==0);
    assert(uxpipe_read(&reader,one,1,0)==0);
    reset();
    for(i=0;i<10000;++i)payload[i]=(unsigned char)(i*37u+17u);
    assert(pthread_create(&thread,0,producer,0)==0);
    while ((n=uxpipe_read(&reader,received+total,73,-1))>0) {
        total+=(unsigned int)n;
        assert(total<=10000);
    }
    assert(n==0 && total==10000);
    assert(pthread_join(thread,0)==0);
    assert(!memcmp(received,payload,10000));
    assert(waits>0 && wakes>0);
    reset();
    assert(uxpipe_finish(&reader)==0);
    assert(uxpipe_write(&writer,payload,1,0)==-1);
    reset();
    injected_gone=1;
    assert(uxpipe_read(&reader,one,1,-1)==-1);
    injected_gone=0;
    data.magic=0;
    assert(uxpipe_read(&reader,one,1,-1)==-1);
    puts("PASS: SPSC stream, wraparound, backpressure, EOF, timeout, peer close and corruption");
    return 0;
}
