/* Stage-3 F5 shell entry. unix-init supplies an initialized PVD.
 * It is deliberately a same-process builtin interpreter; no Unix exec,
 * environment or POSIX fd table is claimed.
 */
#include "libbb.h"
#include "f5_shell.h"
#include "pvd_cli.h"

extern int plan2001_busybox_dispatch(int argc, char **argv);

static int write_bytes(const char *bytes, size_t length)
{
    return full_write(1, bytes, length) == (ssize_t)length ? 0 : -1;
}

static int write_text(const char *text)
{
    return write_bytes(text, strlen(text));
}

static int prompt(const Pvd *view)
{
    char path[PVD_PATH_MAX];
    if (pvd_getcwd(view, path, sizeof(path)) != PVD_OK)
        return -1;
    if (write_text("unix:") < 0 || write_text(path) < 0 ||
        write_text("$ ") < 0)
        return -1;
    return 0;
}

int p2001_f5_shell(Pvd *view, void *context)
{
    char *echo_args[] = {
        "busybox", "echo", "PLAN2001-BUSYBOX-F5-READY", 0
    };
    char line[128], root[2], ch;
    static const char read_marker[] = "PLAN2001-BUSYBOX-F5-READ:";
    unsigned int used = 0;
    long n;
    (void)context;

    if (!view || pvd_getcwd(view, root, sizeof(root)) != PVD_OK ||
        root[0] != '/' || root[1] != 0)
        return -1;
    if (plan2001_busybox_dispatch(3, echo_args) != 0)
        return -1;
    if (write_text("PLAN2001-PVD-READY\n") < 0 ||
        write_text("PLAN2001-UNIX-INIT-ROOT:/\n") < 0 ||
        write_text("PLAN2001-UNIX-SHELL-READY\n") < 0 ||
        prompt(view) < 0)
        return -1;
    for (;;) {
        n = full_read(0, &ch, 1);
        if (n != 1)
            return -1;
        if (ch == '\r' || ch == '\n') {
            line[used] = 0;
            if (write_text("\n") < 0 ||
                write_text(read_marker) < 0 ||
                write_bytes(line, used) < 0 ||
                write_text("\n") < 0)
                return -1;
            if (pvd_cli_run(view, line) < 0 || prompt(view) < 0)
                return -1;
            used = 0;
        } else if (ch == 8 || ch == 127) {
            if (used) {
                --used;
                if (write_text("\b \b") < 0)
                    return -1;
            }
        } else if ((unsigned char)ch >= 32 && (unsigned char)ch < 127) {
            if (used < sizeof(line) - 1) {
                line[used++] = ch;
                if (write_bytes(&ch, 1) < 0)
                    return -1;
            }
        }
    }
}
