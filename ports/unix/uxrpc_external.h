#ifndef PLAN2001_UXRPC_EXTERNAL_H
#define PLAN2001_UXRPC_EXTERNAL_H
#include "uxservice.h"
/* Execute an untrusted file client, delegating ONLY its two UXPIPE channels
 * and Vexecute. The caller's UxService remains the single GE/O owner.
 */
int uxrpc_external_run(UxService *service,int exec_volume,UxOid exec_oid,
                       char *const *argv,unsigned int argc,void *output,
                       unsigned int capacity,unsigned int *size);
#endif
