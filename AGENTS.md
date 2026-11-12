# Plan2001 userland development instructions

This repository implements a familiar Unix-facing userspace **over** Plan2001. It does not define the kernel's native architecture.

Read `README.md`, `docs/ARCHITECTURE.md`, `docs/POSIX-PROFILE.md`, and `docs/PORTING.md`, and `docs/BUILD.md`. For current native ABI consult [kernel architecture](https://github.com/plan2001-kernel/kernel/blob/main/docs/ARCHITECTURE.md), [ObjectSpace](https://github.com/plan2001-kernel/kernel/blob/main/docs/OBJECTSPACE.md), and [kernel interface](https://github.com/plan2001-kernel/kernel/blob/main/docs/KERNEL-INTERFACE.md).

## Rules

1. Keep Unix paths, cwd/PVD, file descriptors and POSIX semantics in **userland**.
2. Preserve native ObjectSpace/capability authorization. Never grant access from a name, raw OID, synthetic uid/gid, or OwnerID equality.
3. Do not add a Linux syscall ABI, kernel VFS/path model, Plan 9 Chan/9P, kernel Pipe or ObjectFIFO for the port.
4. Native IPC is region publication with WAIT/WAKE. POSIX streams, if needed, are userspace protocols.
5. Do not silently link Debian libc/startup objects or Linux syscalls into target code.
6. Static BusyBox still requires some C runtime code. Audit license and provenance before importing upstream or kernel-derived code.
7. Keep direct native ObjectSpace APIs available for applications that do not use POSIX.
8. Never silently reformat GE/O volumes.
9. Use English for agent tasks and code-facing docs. Make bounded PRs and do not merge without explicit authorization.
10. State exact tests run, their results and **NOT RUN** limitations. Compilation is not runtime evidence.
11. Use [`config/sources.json`](config/sources.json) as the sole source for immutable external kernel and BusyBox commits. Never silently update them or vendor the kernel into userland.
12. The current CI builds a Linux-host static BusyBox **control**, not a Plan2001 ELF. A successful host check must never be presented as successful Plan2001 libc linkage or QEMU boot.

**First implementation task:** audit a deliberately minimal BusyBox compile/link on Debian against Plan2001's current ABI and C runtime.
