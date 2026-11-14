# Plan 2001: real Newlib libc port and acceptance criteria

Status: active experimental branch `experiment/newlib-ci`. Pinned kernel ABI:
`82814608dff31f2b33e34689821c468b69d7641e`. Newlib release:
`newlib-4.6.0` (Git commit `8ba4275b83ec27529f67e0d477611fa6d8d6e6bd`).

## Goal

Build the complete upstream Newlib `libc.a` and `libm.a` for the
Plan 2001 native AMD64 ABI, link and execute genuinely Newlib-backed
applications in the real Plan 2001 QEMU guest. Do **not** link
`libc-user.a` to hide unported runtime symbols. Preserve the current
kernel's object/region and process semantics, with no kernel modifications.

## Phases and acceptance

1. **Build upstream Newlib** from its immutable Git tag with no host
   C library dependency in target objects. Archive audit must include
   Newlib stdio, malloc, string and reentrancy components. Preserve
   build logs and source provenance. Fail on missing or suspect archives.
2. **Implement the libgloss target boundary** directly against the
   pinned Plan 2001 kernel ABI:
   `_read`, `_write`, `_sbrk`, `_exit`; add the required
   `_close`, `_fstat`, `_isatty`, `_lseek`, `_getpid`,
   `_kill`, etc. as real adapters where supported, otherwise
   explicit `ENOSYS`/appropriate failure. No fake POSIX-success stubs.
   `errno` is Newlib-owned and no longer defined by a test harness.
3. **Native startup and link**: adapt the existing native ELF entry
   convention with a tiny target crt/start object. Link against the
   complete actual Newlib `libc.a` and `libm.a`, not the old
   Plan 2001 `libc-user.a`. Validate ET_EXEC, no loader/undefined
   symbols, and the kernel-supported PT_LOAD layout.
4. **QEMU acceptance**: boot an ordinary native Newlib executable with
   Newlib `printf/snprintf`, `malloc/free`, `strtol`,
   `errno`, and native output; verify serial markers and fail on
   guest errors. Test `_read` with an explicitly delegated input
   region, and `_exit(0)` / `_exit(nonzero)` using process Handle
   observations in ordinary spawned children.
5. **Regression, documentation, and review**: run existing
   Newlib-subset/libgloss smoke tests, collect downloadable CI logs,
   document limitations and build instructions, leave draft PR
   unmerged. Every reported PASS must correspond to a real CI check.

## Deliberate boundaries

- This first native port is single-threaded. Multithreaded locking,
  shared errno/reentrancy and concurrent stdio require separate work.
- POSIX filenames, arbitrary file-descriptor I/O, signals, TTY control,
  process numeric exit codes, and complete GNU/BSD userspace are not
  invented merely to satisfy Newlib headers. Unsupported operations
  return meaningful errors. Plan 2001 process success/failure cannot
  represent arbitrary numeric Unix exit status yet.
- Newlib's arch-optimized assembly routines must be compatible with the
  Plan 2001 AMD64 ABI, including reserved registers and runtime entry.
  Native guest execution is mandatory.
- Avoid reformatting any existing GE/O volume: QEMU CI creates
  disposable regular-file images only.
