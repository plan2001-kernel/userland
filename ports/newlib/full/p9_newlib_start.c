/* Plan2001 OEXECUTE ELF entry is in the pinned kernel's startup.S.
 * p9_start runs before any Newlib stdio/heap use, so Newlib can lazily
 * initialize its single-thread reentrancy records. Never use Plan9 libc.
 */
#include <stdlib.h>

extern char *argv0;
int main(int argc, char **argv);
extern int p9_full_auto_bind_uxpipe(void);

_Noreturn void p9_start(int argc, char **argv)
{
    if(argc > 0)
        argv0 = argv[0];
    if(p9_full_auto_bind_uxpipe() < 0)
        exit(125); /* incomplete or excessive inherited stream grants */
    exit(main(argc, argv));
    __builtin_unreachable();
}
