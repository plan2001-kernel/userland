/* Optional native external applet executor for a granted Unix view.
 * The launch authority and executable OID are supplied by an explicit
 * higher-privileged userland bootstrap; paths NEVER mint GE/O authority.
 */
#ifndef PLAN2001_UXEXTERNAL_H
#define PLAN2001_UXEXTERNAL_H
#include "uxfs.h"
typedef struct {
    UxFs *fs;
    int executable_volume;          /* authorized Vexecute GE/O Handle */
    UxOid executable_oid;           /* identity only */
} UxExternal;
int uxexternal_ls(void *ctx, const char *command, char *const *argv,
                  unsigned int argc, void *output, unsigned int capacity,
                  unsigned int *size);
#endif
