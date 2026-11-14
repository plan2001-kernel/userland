#ifndef PLAN2001_F5_SHELL_H
#define PLAN2001_F5_SHELL_H
#include "../unix/uxfs.h"
typedef struct {
    int volume;               /* explicitly delegated authorized GE/O Handle */
    UxOid manifest;           /* existing, caller-provisioned manifest OID */
    int restore;              /* 1: manifest must already exist and validate */
} UxF5Storage;

/* Bounded F5 interpreter. NOT upstream BusyBox ash/sh. A non-NULL
 * context may point to UxF5Storage to opt into persistent GE/O I/O.
 */
int p2001_f5_shell(Pvd *view, void *context);
#endif
