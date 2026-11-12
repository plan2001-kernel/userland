# Debian-first BusyBox compilation experiment

**Planned test. Not yet executed.**

## Goal

Discover whether a tiny BusyBox configuration can compile and statically link against Plan2001's existing C runtime and thin POSIX adapters **without importing Debian's host libc**. Document missing interfaces rather than prematurely implementing all of POSIX.

## Inputs

* Kernel project: [plan2001-kernel/kernel](https://github.com/plan2001-kernel/kernel)
* Inspect `runtime/libc/port`, `runtime/libc/9sys`, `runtime/abi` and the actual kernel user-program build/startup/linker sources.
* BusyBox upstream: https://busybox.net/ . Pin an exact release and checksum before implementing a reproducible build.
* Initial target: Plan2001 AMD64 userspace ELF. ARM64 target support must be verified separately.

## Stage A: compile and dependency inventory on Debian

1. Inspect current Plan2001 executable loader, entry ABI, stack, argv, startup code, syscall stubs and target toolchain.
2. Select only a tiny BusyBox applet set, initially `true`, `false` and `echo` if they fit.
3. Attempt compilation using target headers/runtime, recording all missing headers, functions and nonportable assumptions.
4. Classify blockers by C runtime, POSIX fd/path/console, process/environment, ELF startup, toolchain support, and compiler runtime.
5. Attempt static linking. `-nostdlib` avoids automatic startup/libc, but the build must explicitly supply startup code and any required GCC support objects.
6. Inspect the artifact:

```sh
file path/to/busybox
readelf -hW path/to/busybox
readelf -lW path/to/busybox
readelf -dW path/to/busybox
nm -u path/to/busybox
```

A valid static target should not accidentally have a Linux `PT_INTERP`, Linux shared dependencies, Linux startup/TLS assumptions, unresolved target symbols or mismatched entry ABI. Default Debian GCC targeting AMD64 is not proof of Plan2001 compatibility.

A normal host BusyBox linked to Debian libc is only a baseline control.

## Stage B: adapt minimal userspace

Use minimal C runtime functions and explicit, auditable Plan2001 POSIX adapters. Never return fake success for unimplemented operations. Do not modify kernel ABI solely to make a link pass.

## Stage C: execute on Plan2001 QEMU

Create an authorized executable object, provide the necessary capabilities, boot the real Plan2001 kernel, and verify stdout, exit status, `argv[0]` multicall selection, and eventually GE/O-backed file operations. A Debian host execution is not a kernel execution test.

## Acceptance evidence

Each implementation PR should state the upstream BusyBox revision/config, compiler and linker versions/options, kernel ABI revision, C runtime sources, exact link diagnostics, host tests, QEMU tests, hardware tests and **NOT RUN** items.

The first implementation PR is the **compile/link audit**, not the entire shell or POSIX filesystem.
