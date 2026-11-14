#ifndef PLAN2001_UXRPC_CHILD_H
#define PLAN2001_UXRPC_CHILD_H
/* Dedicated, unprivileged native child with four UXPIPE region grants and
 * one Vexecute grant. No GE/O storage Handle is ever inherited here.
 */
int p2001_uxrpc_child_main(int argc, char **argv);
#endif
