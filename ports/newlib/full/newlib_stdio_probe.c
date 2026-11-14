/* Native executable backed exclusively by upstream Newlib libc.a/libm.a.
 * Real QEMU guest checks stdio, malloc, errno, string, math, capability
 * stdin _read and native console _write. Failure means no PASS marker.
 */
#include <errno.h>
#include <stddef.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "obj.h"
#include "plan2001_full.h"
#include "../../unix/uxpipe.h"

extern int _fstat(int fd, struct stat *st);
extern int _open(const char *path, int flags, ...);
extern void _exit(int status);
extern int _read(int fd, char *buf, int count);
extern off_t _lseek(int fd, off_t offset, int whence);
extern void *_sbrk(ptrdiff_t change);

static void check(int predicate, const char *what)
{
    if(!predicate) {
        fprintf(stderr, "PLAN2001-NEWLIB-FULL-FAIL: %s\n", what);
        fflush(stderr);
        _exit(9);
    }
}


/* Test the *actual* native Plan2001 region maps and the supervisor's
 * UXPIPE format, not a Linux pipe or synthetic success. */
static void check_uxpipe(void)
{
    int handles[6];
    uintptr_t maps[6];
    unsigned i;
    static const char payload[] = "newlib-uxpipe-input";
    static const char output[] = "newlib-uxpipe-output";
    char readback[sizeof(payload)];
    struct stat st;
    for(i=0;i<6;++i) {
        long h = p9_full_region_create(UXPIPE_PAGE);
        check(h>0, "create UXPIPE region");
        handles[i]=(int)h;
        maps[i]=(uintptr_t)p9_full_region_map(handles[i],0);
        check(maps[i]!=0 && maps[i]!=(uintptr_t)-1, "initialize UXPIPE");
    }
    for(i=0;i<3;++i)
        check(uxpipe_format((UxPipeData *)maps[2*i],
                            (UxPipeAck *)maps[2*i+1])==0, "format UXPIPE");
    /* Simulate an already-published stdin + explicit producer EOF. */
    memcpy(((UxPipeData *)maps[0])->payload,payload,sizeof(payload));
    __atomic_store_n(&((UxPipeData *)maps[0])->produced,
                     sizeof(payload),__ATOMIC_RELEASE);
    __atomic_store_n(&((UxPipeData *)maps[0])->closed,1,__ATOMIC_RELEASE);
    for(i=0;i<6;++i)
        check(p9_full_region_unmap(maps[i])==0, "transfer UXPIPE writers");
    check(p9_full_bind_uxpipe(handles)==0, "bind supervisor stream grants");
    check(_fstat(1,&st)==0 && S_ISFIFO(st.st_mode), "stdout UXPIPE fstat");
    check(read(0,readback,sizeof readback)==sizeof(payload),
          "read stdin UXPIPE bytes");
    check(memcmp(readback,payload,sizeof(payload))==0,"stdin content");
    check(read(0,readback,1)==0,"stdin UXPIPE explicit EOF");
    check(write(1,output,sizeof(output))==sizeof(output),
          "write Newlib stdout UXPIPE");
    check(write(2,output,sizeof(output))==sizeof(output),
          "write Newlib stderr UXPIPE");
    check(close(1)==0,"publish Newlib stdout EOF");
    check(close(2)==0,"publish Newlib stderr EOF");
    check(_lseek(0,0,0)==-1 && errno==ESPIPE,"UXPIPE is not seekable");
    /* Read-only observers are permitted while the producer has RW. */
    for(i=1;i<3;++i) {
        long mapped=p9_full_region_map(handles[2*i],1);
        UxPipeData *p;
        check(mapped>0,"observe published UXPIPE");
        p=(UxPipeData *)(uintptr_t)mapped;
        check(p->produced==sizeof(output) && p->closed==1 &&
              memcmp(p->payload,output,sizeof(output))==0,
              "Newlib output bytes reached delegated regions");
        check(p9_full_region_unmap((uintptr_t)mapped)==0,"unmap observer");
    }
    p9_full_unbind_uxpipe();
    for(i=0;i<6;++i)
        check(p9_full_region_close(handles[i])==0,"close UXPIPE handle");
    printf("PLAN2001-NEWLIB-FULL-UXPIPE-PASS\n");
    fflush(stdout);
}

int main(int argc, char **argv)
{
    char tmp[100];
    char *p, *endp;
    long region, write_map;
    ObjRingHeader *hdr;
    ObjRingSlot *slot;
    static const char source[] = "libc-stdin-test\n";
    volatile double theta = 0.5;
    double v;
    long value;
    struct stat st;
    struct timeval before, after;
    long long elapsed_us;
    (void)argc; (void)argv;
    errno = 0;
    check(_read(0, tmp, 1) == -1 && errno == EBADF,
          "stdin must require an explicit capability");
    check(_sbrk(0) != (void *)-1, "BRK_ used by Newlib allocator");
    check(sbrk(0) == _sbrk(0), "sbrk POSIX forward shares break");

    check(snprintf(tmp, sizeof tmp, "answer:%d:%s", 42, "newlib") == 16,
          "snprintf length");
    check(strcmp(tmp, "answer:42:newlib") == 0, "snprintf content");
    printf("PLAN2001-NEWLIB-FULL-PRINTF-PASS %s\n", tmp);
    check(write(1, "", 0) == 0, "public write forwards to native adapter");
    fflush(stdout);

    p = (char *)malloc(8192);
    check(p != NULL, "Newlib malloc");
    memset(p, 0x5c, 8192);
    check(p[0] == 0x5c && p[8191] == 0x5c, "Newlib heap data");
    free(p);
    errno = 0;
    value = strtol("12345", &endp, 10);
    check(value == 12345 && *endp == 0 && errno == 0, "strtol");
    printf("PLAN2001-NEWLIB-FULL-MALLOC-PASS\n");
    fflush(stdout);

    v = sin(theta);
    check(v > 0.479 && v < 0.480, "Newlib libm sin");
    printf("PLAN2001-NEWLIB-FULL-LIBM-PASS\n");
    fflush(stdout);

    check(gettimeofday(&before, NULL) == 0, "native gettimeofday start");
    check(sleep(1) == 0, "POSIX sleep(1) must succeed");
    check(gettimeofday(&after, NULL) == 0, "native gettimeofday end");
    elapsed_us = ((long long)after.tv_sec - (long long)before.tv_sec)*1000000LL +
        ((long long)after.tv_usec - (long long)before.tv_usec);
    check(elapsed_us >= 500000LL, "sleep(1) must not mean 1 millisecond");
    check(elapsed_us < 20000000LL, "sleep(1) unexpectedly too long");
    printf("PLAN2001-NEWLIB-FULL-TIME-PASS\n");
    fflush(stdout);

    check(_fstat(1, &st) == 0 && S_ISCHR(st.st_mode), "native stdout fstat");
    errno = 0;
    check(_open("/plan2001-not-ambient", 0) == -1 && errno == ENOSYS,
          "unsupported pathname open is ENOSYS");
    printf("PLAN2001-NEWLIB-FULL-ERRNO-PASS\n");
    fflush(stdout);

    region = p9_full_region_create(OBJ_RING_BYTES);
    check(region > 0, "create native input region");
    write_map = p9_full_region_map((int)region, 0);
    check(write_map > 0, "map writable native input ring");
    hdr = (ObjRingHeader *)(unsigned long)write_map;
    memset(hdr, 0, sizeof *hdr);
    hdr->version = OBJ_RING_VERSION;
    hdr->magic = OBJ_RING_MAGIC;
    hdr->slot_size = OBJ_RING_SLOT;
    hdr->slots = OBJ_RING_SLOTS;
    slot = (ObjRingSlot *)(unsigned long)(write_map + OBJ_RING_HEADER);
    __atomic_store_n(&slot->seq, 1, __ATOMIC_RELAXED);
    slot->length = sizeof(source)-1;
    slot->flags = 0;
    memcpy((char *)(slot + 1), source, sizeof(source)-1);
    __atomic_store_n(&slot->seq, 2, __ATOMIC_RELEASE);
    __atomic_store_n(&hdr->write_seq, 1, __ATOMIC_RELEASE);
    check(p9_full_bind_stdin((int)region) == 0, "bind stdin capability");
    check(read(0, tmp, 0) == 0, "public read forwards to native adapter");
    check(fgets(tmp, sizeof tmp, stdin) != NULL, "Newlib fgets from region");
    check(strcmp(tmp, source) == 0, "Newlib stdin content");
    p9_full_unbind_stdin();
    check(p9_full_region_unmap((uintptr_t)write_map) == 0,
          "unmap writer region");
    check(p9_full_region_close((int)region) == 0, "close owned region");
    printf("PLAN2001-NEWLIB-FULL-STDIN-PASS\n");
    fflush(stdout);

    check_uxpipe();
    printf("PLAN2001-NEWLIB-FULL-PASS\n");
    fflush(stdout);
    for(;;)
        (void)sleep(1);
}
