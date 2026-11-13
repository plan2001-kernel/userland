#ifndef PLAN2001_F5_SHELL_H
#define PLAN2001_F5_SHELL_H
#include "../unix/pvd.h"

/* Interim F5 builtin command reader. NOT upstream BusyBox ash/sh. */
int p2001_f5_shell(Pvd *view, void *context);
#endif
