/* Minimal Plan2001 multicall dispatcher for a pinned subset of unmodified
 * BusyBox 1.36.1 applets. No Linux appletlib or Linux CRT is linked.
 * General BusyBox / POSIX support is explicitly out of scope.
 */
#include "libbb.h"

extern int true_main(int argc, char **argv);
extern int false_main(int argc, char **argv);
extern int echo_main(int argc, char **argv);
extern int strcmp(const char *a, const char *b);
extern long sleep(long millis);
extern void exits(char *);

static const char *last_component(const char *name)
{
    const char *part = name;
    while (*name) {
        if (*name++ == '/')
            part = name;
    }
    return part;
}

int plan2001_busybox_dispatch(int argc, char **argv)
{
    const char *name;
    if (argc < 1 || !argv || !argv[0])
        return 64;
    name = last_component(argv[0]);
    if (strcmp(name, "busybox") == 0) {
        if (argc < 2 || !argv[1])
            return 64;
        argc--;
        argv++;
        name = last_component(argv[0]);
    }
    if (strcmp(name, "true") == 0)
        return true_main(argc, argv);
    if (strcmp(name, "false") == 0)
        return false_main(argc, argv);
    if (strcmp(name, "echo") == 0)
        return echo_main(argc, argv);
    return 127;
}

#ifdef PLAN2001_BUSYBOX_GUEST_SMOKE
/* Dedicated *guest* fixture. It calls the actual linked upstream applets
 * and proves stdout and return values. Kernel boot-owner cannot exit yet,
 * so retain it alive only AFTER the success marker was emitted.
 */
static void fail_guest(void)
{
    static const char msg[] = "PLAN2001-BUSYBOX-GUEST-FAIL\n";
    (void)full_write(1, msg, sizeof(msg) - 1);
    exits("busybox smoke failed");
    for (;;)
        (void)sleep(1000);
}

int main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
    char *echo_args[] = {
        "busybox", "echo", "PLAN2001-BUSYBOX-ECHO-HELLO", 0
    };
    char *echo_alias[] = {
        "/bin/echo", "PLAN2001-BUSYBOX-ALIAS-OK", 0
    };
    char *true_args[] = { "busybox", "true", 0 };
    char *false_args[] = { "busybox", "false", 0 };
    char *unknown_args[] = { "busybox", "unknown", 0 };
    static const char passed[] = "PLAN2001-BUSYBOX-GUEST-PASS\n";

    if (plan2001_busybox_dispatch(3, echo_args) != 0)
        fail_guest();
    if (plan2001_busybox_dispatch(2, echo_alias) != 0)
        fail_guest();
    if (plan2001_busybox_dispatch(2, true_args) != 0)
        fail_guest();
    if (plan2001_busybox_dispatch(2, false_args) != 1)
        fail_guest();
    if (plan2001_busybox_dispatch(2, unknown_args) != 127)
        fail_guest();
    if (full_write(1, passed, sizeof(passed) - 1)
            != sizeof(passed) - 1)
        fail_guest();
    for (;;)
        (void)sleep(1000);
}
#else
int main(int argc, char **argv)
{
    return plan2001_busybox_dispatch(argc, argv);
}
#endif
