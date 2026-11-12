# Plan2001 Userland

A small Unix-like userspace built on the experimental [Plan2001 kernel](https://github.com/plan2001-kernel/kernel).

**Status:** Design and porting plan. No BusyBox port or POSIX layer is implemented yet.

## Goal

Create a familiar Unix command-line environment using a statically linked, selectively configured BusyBox. Alpine Linux minimal base is the initial user-experience reference; OpenBSD base provides a secondary reference for Unix utility behavior. Neither Linux nor OpenBSD kernel ABI compatibility is required.

Plan2001's ObjectSpace, capability permissions, GE/O storage, and region-based IPC remain native. Unix paths, file descriptors, and a working-directory view belong in userland.

## Architecture

```text
static BusyBox ELF (applets + minimal libc + POSIX adapters)
                        |
       userspace paths / PVD / fd table
                        |
        Plan2001 native object ABI
                        |
     ObjectSpace / GE/O / capabilities
                        |
                 Plan2001 kernel
```

Static linking means the necessary C runtime functions are included inside the executable. It does not eliminate the need for libc functionality.

The familiar `/bin`, `/usr`, `/etc`, and `/tmp` directories are userland projections of ObjectSpace bindings, **not kernel directories**. Multiple bindings can refer to one BusyBox executable GE/O object; an applet launcher must preserve the requested `argv[0]`.

## First experiment

On a Debian development host, inspect the existing kernel `runtime/libc`, select a tiny BusyBox configuration, and attempt compilation and static linking without Debian's libc. Inventory missing C and POSIX interfaces and check ELF startup and dependencies. Successful linking is not evidence of working execution under Plan2001; verify the result in the real Plan2001 kernel with QEMU.

## Milestones (proposed)

1. Debian BusyBox dependency and static-link audit.
2. Minimal Plan2001 C runtime and POSIX adapters.
3. Small static applets running in Plan2001 QEMU.
4. ObjectSpace-backed paths and commands such as `ls`, `cat`, `cp`, `rm`, `mkdir`.
5. Shell process launch, redirection, userland pipes, and a broader Unix subset.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Minimal POSIX profile](docs/POSIX-PROFILE.md)
- [BusyBox porting plan](docs/PORTING.md)
- [Development instructions](AGENTS.md)

The authoritative native definitions are in the [kernel ObjectSpace architecture](https://github.com/plan2001-kernel/kernel/blob/main/docs/OBJECTSPACE.md) and [kernel interface](https://github.com/plan2001-kernel/kernel/blob/main/docs/KERNEL-INTERFACE.md).

## Licensing

BusyBox is GPLv2-licensed. Record source provenance and licenses before importing BusyBox sources or kernel runtime code. No third-party sources are included at this stage.
