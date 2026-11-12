# Plan2001 Userland

A small Unix-like userspace built on the experimental [Plan2001 kernel](https://github.com/plan2001-kernel/kernel).

**Status:** CI dependency fetch, native ABI inventory and Linux-host BusyBox static control are implemented. **No BusyBox port to Plan2001 or POSIX layer is implemented yet.**

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

## Build and CI workspace

The [build mechanism](docs/BUILD.md) pins the external kernel and BusyBox sources in [`config/sources.json`](config/sources.json). GitHub Actions fetches each as a **sibling source tree** outside the userland repo, verifies the exact commits, inspects the kernel's `runtime/libc` and native ABI, and builds a minimal **Linux-host static BusyBox control** (`true`, `false`, `echo`). That control is intentionally linked against the Linux runner's libc and is **not** a Plan2001 program.

The next implementation objective is to compile and statically link BusyBox **against Plan2001's own runtime without Linux libc**, then execute it under the real Plan2001 kernel in QEMU. Current CI must report these target tests **NOT RUN** until implemented.

## Milestones (proposed)

1. Debian BusyBox dependency and static-link audit.
2. Minimal Plan2001 C runtime and POSIX adapters.
3. Small static applets running in Plan2001 QEMU.
4. ObjectSpace-backed paths and commands such as `ls`, `cat`, `cp`, `rm`, `mkdir`.
5. Shell process launch, redirection, userland pipes, and a broader Unix subset.

## Documentation

- [Build mechanism and local Debian commands](docs/BUILD.md)
- [GitHub Actions build workflow](.github/workflows/build.yml)
- [Architecture](docs/ARCHITECTURE.md)
- [Minimal POSIX profile](docs/POSIX-PROFILE.md)
- [BusyBox porting plan](docs/PORTING.md)
- [Development instructions](AGENTS.md)

The authoritative native definitions are in the [kernel ObjectSpace architecture](https://github.com/plan2001-kernel/kernel/blob/main/docs/OBJECTSPACE.md) and [kernel interface](https://github.com/plan2001-kernel/kernel/blob/main/docs/KERNEL-INTERFACE.md).

## Licensing

BusyBox is GPLv2-licensed. Record source provenance and licenses before importing BusyBox sources or kernel runtime code. No third-party sources are included at this stage.
