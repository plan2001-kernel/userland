/* Minimal Plan2001 multicall dispatcher for a pinned subset of unmodified
 * BusyBox 1.36.1 applets. No Linux appletlib or Linux CRT is linked.
 * General BusyBox / POSIX support is explicitly out of scope.
 */
#include "libbb.h"
#ifdef PLAN2001_BUSYBOX_F5
#include "pvd_cli.h"
#endif

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

#ifdef PLAN2001_BUSYBOX_F5
/* A bounded interactive I/O acceptance launcher, NOT ash/sh or libbb cat.
 * Runs real upstream echo_main before reading native console stdin.
 * One process lives in manifest slot 4 (Ctrl+Alt+F5). */
int main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
    char *args[] = { "busybox", "echo", "PLAN2001-BUSYBOX-F5-READY", 0 };
    char line[128];
    static Pvd view;
    unsigned int used = 0;
    char ch;
    long n;
    static const char prompt[] = "busybox> ";
    static const char reply[] = "PLAN2001-BUSYBOX-F5-READ:";
    if (plan2001_busybox_dispatch(3, args) != 0)
        exits("F5 echo failed");
    if (pvd_bootstrap(&view) != PVD_OK)
        exits("F5 PVD bootstrap failed");
    if (full_write(1, "PLAN2001-PVD-READY\n", 19) != 19)
        exits("F5 PVD announcement failed");
    if (full_write(1, prompt, sizeof(prompt)-1) != sizeof(prompt)-1)
        exits("F5 prompt failed");
    for (;;) {
        n = full_read(0, &ch, 1);
        if (n != 1)
            exits("F5 stdin unavailable");
        if (ch == '\r' || ch == '\n') {
            line[used] = 0;
            if (full_write(1, "\n", 1) != 1 ||
                full_write(1, reply, sizeof(reply)-1) != sizeof(reply)-1 ||
                full_write(1, line, used) != (ssize_t)used ||
                full_write(1, "\n", 1) != 1)
                exits("F5 response failed");
            if (pvd_cli_run(&view, line) < 0)
                exits("F5 PVD output failed");
            if (full_write(1, prompt, sizeof(prompt)-1) != sizeof(prompt)-1)
                exits("F5 prompt failed");
            used = 0;
        } else if (ch == 8 || ch == 127) {
            if (used) {
                --used;
                (void)full_write(1, "\b \b", 3);
            }
        } else if ((unsigned char)ch >= 32 && (unsigned char)ch < 127) {
            if (used < sizeof(line)-1) {
                line[used++] = ch;
                if (full_write(1, &ch, 1) != 1)
                    exits("F5 keyboard echo failed");
            }
        }
    }
}
#elif defined(PLAN2001_BUSYBOX_GUEST_SMOKE)
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
