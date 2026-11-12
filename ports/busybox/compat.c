/* Plan2001 native capability-based console bridge for the initial BusyBox
 * applet subset. No POSIX file descriptors or Linux syscalls are involved.
 * STDOUT_FILENO is only a userspace convention for this narrow adapter.
 * Source is part of the userland project; BusyBox applets remain upstream.
 */
#include "libbb.h"
#include "obj.h"

extern long obj_open(unsigned long long, void *, long, unsigned long long, ObjId *);
extern long obj_write(int, void *, unsigned long long, unsigned long long);
extern long obj_close(int);
extern void *malloc(unsigned long);
extern void exits(char *);

int errno;
const char bb_msg_write_error[] = "native stdout write failed";

static int out_handle = -1;

static int open_stdout(void)
{
    ObjOpenConsoleReq req = { 1, 0 };
    long h;
    if (out_handle >= 0)
        return out_handle;
    h = obj_open(ObjOpenConsole, &req, sizeof(req), 0, 0);
    if (h < 0)
        return -1;
    out_handle = (int)h;
    return out_handle;
}

/* Return short writes to the caller as a real failure, never fake success. */
ssize_t full_write(int fd, const void *buf, size_t count)
{
    const unsigned char *p = (const unsigned char *)buf;
    size_t total = 0;
    int h;
    if (fd != STDOUT_FILENO || (h = open_stdout()) < 0)
        return -1;
    while (total < count) {
        long n = obj_write(h, (void *)(p + total), count - total, 0);
        if (n <= 0 || (size_t)n > count - total)
            return -1;
        total += (size_t)n;
    }
    return (ssize_t)total;
}

void *xmalloc(size_t size)
{
    void *p = malloc(size ? size : 1);
    if (!p)
        exits("BusyBox xmalloc failed");
    return p;
}

char *stpcpy(char *dest, const char *src)
{
    while ((*dest = *src) != 0) {
        ++dest;
        ++src;
    }
    return dest;
}

void bb_simple_perror_msg(const char *message)
{
    const char prefix[] = "BusyBox: ";
    (void)full_write(1, prefix, sizeof(prefix) - 1);
    (void)full_write(1, message, strlen(message));
    (void)full_write(1, "\n", 1);
}
