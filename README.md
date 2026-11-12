# Plan2001 Userland

A small Unix-like userspace built on the experimental [Plan2001 kernel](https://github.com/plan2001-kernel/kernel).

**Status:** Pinned source CI and libc symbol audit are implemented. A new first-stage native target workflow builds original BusyBox `true`/`false`/`echo` applets with Plan2001's own libc and tests them on the real kernel in QEMU. This is a **limited subset launcher**, not the complete upstream BusyBox multicall port or POSIX layer.

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

The native diagnostic workflow links the upstream BusyBox `true.c` applet (not the full multicall binary) against the actual Plan2001 runtime and runs separate guest checks. The dedicated [libc ABI audit](docs/libc-audit.md) compares host-compiled BusyBox object references with native library exports. These checks are not evidence of complete POSIX compatibility.

## Milestones (proposed)

1. Debian BusyBox dependency and static-link audit.
2. Minimal Plan2001 C runtime and POSIX adapters.
3. Small static applets running in Plan2001 QEMU.
4. ObjectSpace-backed paths and commands such as `ls`, `cat`, `cp`, `rm`, `mkdir`.
5. Shell process launch, redirection, userland pipes, and a broader Unix subset.

## Documentation

- [Build mechanism and local Debian commands](docs/BUILD.md)
- [GitHub Actions libc ABI audit and evidence](docs/libc-audit.md)
- [Native BusyBox target subset build and QEMU smoke test](docs/BUSYBOX-TARGET.md)
- [GitHub Actions build workflow](.github/workflows/build.yml)
- [Architecture](docs/ARCHITECTURE.md)
- [Minimal POSIX profile](docs/POSIX-PROFILE.md)
- [BusyBox porting plan](docs/PORTING.md)
- [Development instructions](AGENTS.md)

The authoritative native definitions are in the [kernel ObjectSpace architecture](https://github.com/plan2001-kernel/kernel/blob/main/docs/OBJECTSPACE.md) and [kernel interface](https://github.com/plan2001-kernel/kernel/blob/main/docs/KERNEL-INTERFACE.md).

## Licensing

BusyBox is GPLv2-licensed. Record source provenance and licenses before importing BusyBox sources or kernel runtime code. No third-party sources are included at this stage.
