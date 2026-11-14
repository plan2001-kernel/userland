# Unix stages 4–7: capability-aware file projection and stream boundary

This branch builds on the Stage 3 F5 Unix init (PR #9). The implementation
is intentionally incremental; CI separates **implemented code**, a **host
compatibility/mocked-storage test**, and **native QEMU acceptance**. A green
compiler is not equivalent to a working BusyBox ash or persistent Unix root.

## Stage 4: GE/O persistence contract

`ports/unix/uxfs.[ch]` implements a small in-memory directory/file view.
It normalizes relative paths against the existing PVD and bounds `..` at
the root. Initial limits are 64 total nodes, 16 PVD path levels, 47-byte
labels, and 1024 bytes per regular file. These are not POSIX limits.

Commands in the F5 evaluator are `mkdir`, `cat`, `cp`, `mv`, `rm`, `ls`,
`pwd`, `cd` and `echo`. Files appear in the memory-only view **now**.
`ports/unix/uxgeo.[ch]` provides a separate native persistence backend
requiring an explicitly delegated, authorized GE/O `VkindGeVolume` Handle.
It never opens the boot volume implicitly or authorizes operations from
a raw OID. Calling `uxfs_save(fs, &store, known_manifest_oid)` writes each
file as its own GE/O OID, writes a versioned checksummed manifest, and
commits. `uxfs_restore` reopens every object using that *same explicit*
volume authority, checks lengths/duplicates/checksum, and reconstructs the
in-memory view. The caller must create/provision the durable manifest OID.

**Important unfinished boot integration:** Kernel boot-manifest F5 console
processes have session grants, not ambient GE/O write authority. Consequently
the F5 reader does **not** automatically save files to disk. The stage 4
CI exercises a transactional GE/O *mock*, then constructs a fresh RAM view
and restores from the same mock committed storage. **Real QEMU GE/O file
persistence across reboot: NOT RUN.** Real hardware: NOT RUN.

## Stage 5: upstream BusyBox ash (blocked)

The pinned BusyBox 1.36.1 is compiled by the existing native build only
for upstream `true.c`, `false.c`, and `echo.c`. The stage 5 CI job
separately tries compiling the original `shell/ash.c` with
`-ffreestanding -nostdinc` and the current userland libbb header, and
publishes a compiler diagnostic. An unsuccessful compile is **FAIL** for
that job, not a reason to suppress stage 6/7 jobs. Full BusyBox ash,
`libbb`, POSIX libc, job control, `fork/exec`, and QEMU ash tests are
**NOT IMPLEMENTED**. The small F5 command evaluator is not called ash.

## Stage 6: native SPAWN/descriptor grant boundary

`ports/unix/uxspawn.[ch]` constructs the current kernel SPAWN ABI with
`Ospawn.flags = 2` and an explicit `Oinheritv` grant array, then waits
on the returned `VkindProcess` Handle. The caller must provide an
authorized executable-volume Handle and a real executable OID.
Descriptors are *userland mappings* of delegated region handles, not raw
integer UNIX FDs inherited by the kernel. The three grant slots are limited
to attenuated read/write/meta rights and reject guessed/nonexistent handles
at the kernel call boundary.

The host ABI test uses a deliberately mocked SPAWN and ProcessWait; the
native freestanding translation unit is also compiled. Neither constitutes
proof that `ls` is yet an actual separate native QEMU process. External
execution is enabled only by explicitly installing the
`UxShellIO.external` callback. The normal F5 evaluator keeps `ls`
in-process until a real volume-root launch and stream receiver is wired.
**Independent `ls` in QEMU: NOT RUN.**

## Stage 7: redirection and stream experiments

`ports/unix/uxshell.[ch]` parses short commands, simple quotes, escaped
characters, one `|` separator, and terminal `>` or `>>`. Examples:

```sh
echo test > /tmp/file
cat /tmp/file
echo more >> /tmp/file
cat /tmp/file | cat
cat /tmp/file | cat > /tmp/copy
```

Redirection stores bytes in the bounded file view. The prototype pipe
runs its stages **sequentially** through a bounded byte buffer. It is not
concurrent `pipe(2)`, supports neither multiple pipe stages nor backpressure,
and does not implement `dup2`. Reject unsupported syntax rather than
returning a fake successful POSIX operation.

## Independent CI / acceptance

Workflow: `.github/workflows/unix-stages-4-7.yml`. Each stage uses its
own job, with independent success/failure and explicit NOT RUN reporting.
Stages 4/6/7 can run locally with:

```sh
bash tools/ci/test_unix_stages.sh 4 /path/to/kernel
bash tools/ci/test_unix_stages.sh 6 /path/to/kernel
bash tools/ci/test_unix_stages.sh 7 /path/to/kernel
```

The existing F5 workflow separately compiles the *same* native F5 program
with the new modules and runs the real console test with two QEMU boots
against a disposable GE/O image. That verifies F5's unchanged root startup
and console I/O, **not** durability of new files or upstream ash.

Security invariants: no pathname authority or owner-equality shortcut, no
silent formatting, no added kernel Unix filesystem or pipe, no hidden
Linux CRT, no unvalidated `ObjOpenVolume` call in userland.
