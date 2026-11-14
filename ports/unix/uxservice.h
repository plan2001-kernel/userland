/* Single-owner trusted Unix file service. No ambient GE/O authority:
 * durable operations are enabled only with a caller-provided UxStore whose
 * context holds an authorized native volume capability. A UxService is
 * process-local; cross-process RPC is NOT implemented by this header.
 */
#ifndef PLAN2001_UXSERVICE_H
#define PLAN2001_UXSERVICE_H
#include "uxshell.h"
typedef struct {
    UxFs fs;
    UxStore store;
    UxOid manifest;
    int persistent;
} UxService;
void uxservice_init(UxService *s, Pvd *view);
int uxservice_bind(UxService *s, const UxStore *store, UxOid manifest,
                   int restore);
int uxservice_sync(UxService *s);
int uxservice_command(UxService *s, const char *command, const UxShellIO *io);
#endif
