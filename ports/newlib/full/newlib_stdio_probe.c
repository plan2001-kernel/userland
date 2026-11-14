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
#include "obj.h"
#include "plan2001_full.h"

extern int _fstat(int fd, struct stat *st);
extern int _open(const char *path, int flags, ...);
extern void _exit(int status);
extern int _read(int fd, char *buf, int count);
extern void *_sbrk(ptrdiff_t change);

static void check(int predicate, const char *what)
{
    if(!predicate) {
        fprintf(stderr, "PLAN2001-NEWLIB-FULL-FAIL: %s\n", what);
        fflush(stderr);
        _exit(9);
    }
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

    printf("PLAN2001-NEWLIB-FULL-PASS\n");
    fflush(stdout);
    for(;;)
        (void)sleep(1000);
}
