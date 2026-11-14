/* Real Plan2001 guest probe using unchanged, hash-pinned Newlib sources. */
#include "libbb.h"
extern size_t strlcpy(char *, const char *, size_t);
extern size_t strlcat(char *, const char *, size_t);
extern size_t strnlen(const char *, size_t);
extern int strcmp(const char *, const char *);
extern long sleep(long millis);
extern void exits(char *);

static void emit(const char *message)
{
    size_t n = strlen(message);
    if (full_write(1, message, n) != (ssize_t)n)
        exits("newlib stdout failed");
}
static void fail(void)
{
    emit("PLAN2001-NEWLIB-GUEST-FAIL\n");
    exits("newlib probe failed");
}
int main(void)
{
    char dst[12], small[5];
    const char bounded[3] = { 'a', 'b', 'c' };
    if (strlcpy(dst, "newlib", sizeof(dst)) != 6 ||
        strcmp(dst, "newlib") != 0 ||
        strlcpy(small, "ABCDEFG", sizeof(small)) != 7 ||
        strcmp(small, "ABCD") != 0 ||
        strlcpy(small, "x", 0) != 1)
        fail();
    emit("PLAN2001-NEWLIB-STRLCPY-OK\n");
    if (strlcat(dst, "-ok", sizeof(dst)) != 9 ||
        strcmp(dst, "newlib-ok") != 0 ||
        strlcat(small, "zz", sizeof(small)) != 6 ||
        strcmp(small, "ABCD") != 0)
        fail();
    emit("PLAN2001-NEWLIB-STRLCAT-OK\n");
    if (strnlen("newlib-ok", 3) != 3 ||
        strnlen("newlib-ok", 32) != 9 ||
        strnlen(bounded, sizeof(bounded)) != sizeof(bounded))
        fail();
    emit("PLAN2001-NEWLIB-STRNLEN-OK\n");
    emit("PLAN2001-NEWLIB-GUEST-PASS\n");
    for (;;)
        (void)sleep(1000);
}
