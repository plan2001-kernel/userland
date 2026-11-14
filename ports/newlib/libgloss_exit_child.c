/* Ordinary Plan2001 child process: verify _exit status via Process Handle.
 * No console authority needed in spawned process.
 */
#include "libgloss_plan2001.h"

int
main(int argc, char **argv)
{
    if(argc != 2 || argv == 0 || argv[1] == 0)
        _exit(98);
    if(argv[1][0] == '0' && argv[1][1] == 0)
        _exit(0);
    if(argv[1][0] == '7' && argv[1][1] == 0)
        _exit(7);
    _exit(99);
}
