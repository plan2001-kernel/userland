# Launch BusyBox as a regular child from MicroPython

The [busybox-spawn.yml](../.github/workflows/busybox-spawn.yml) workflow
demonstrates Plan2001's **existing native SPAWN and process-handle wait API**
without replacing MicroPython at the boot executable slot and without any
special BusyBox guest test launcher.

The test uses the immutable kernel and BusyBox commits from
[config/sources.json](../config/sources.json). It builds the existing
three-applet native BusyBox ELF (`true`, `false`, `echo`) using Plan2001
libc and canonical user CRT, not the Debian/Linux runtime.

## Installation and launch

The host-side fixture uses the kernel's own mpy-cross, oseed.py and native
format-o tools to create a **fresh throwaway regular-file GE/O image**:

| Executable | GE/O OID |
| --- | --- |
| Normal native MicroPython boot program | `0x4f45584543000003` |
| Normal static BusyBox ELF with multicall main() | `0x4f45584543000005` |

No GE/O file hierarchy is introduced, and the kernel is not modified. Both
ELF bytes and their SHA-256 hashes are checked against the resulting O image.
This uses the native formatter's generic raw executable seed slot for the
second ELF, with an explicit alternate OID.

The initial Python code is compiled from
[`tests/guest/busybox_spawn_init.py`](../tests/guest/busybox_spawn_init.py)
into the volume's normal init module. The **real MicroPython interpreter**
boots as the kernel's first user process. Its `run()` uses the existing
`space.spawn()` function to start a distinct BusyBox process and
`plan9.process_wait()` to observe the process Handle's immutable completion
record. After validating the child results, the parent prints a PASS marker
and returns to normal MicroPython boot handling.

No shell-process emulation or test-specific C launcher is used for BusyBox.

## Native interactive MicroPython API

For an already-installed, authorized BusyBox executable, a MicroPython
program can launch and wait for it in the same way:

```python
import space, plan9

busybox_oid = 0x4f45584543000005
child = space.spawn(busybox_oid, argv=["busybox", "echo", "hello"])
try:
    result = plan9.process_wait(child, 30000)
    # result = (pid, exit_message, cause, state, generation,
    #           user_ms, sys_ms, real_ms, wait_result, waiters)
    assert result[3] == plan9.OBJ_PROC_COMPLETED
    assert result[2] == plan9.OBJ_PROC_CAUSE_EXIT
    assert result[1] == ""
finally:
    plan9.obj_close(child)
```

The numeric OID is **not authority**: the caller must hold the authorized
GE/O volume capability. The Handle returned by `space.spawn` is authority
for lifecycle observation, not a PID or a filesystem descriptor.

The initial native applet subset's CRT maps exit code zero to an empty
`EXITS` message, and nonzero to `error`. Thus exit status is currently
represented as `0` versus `1` by the demo wrapper, **not** a full POSIX
8-bit exit code. Real terminal fd inheritance and arbitrary libc
`errno` handling are not yet implemented.

## Acceptance evidence

The GitHub Actions workflow:

1. Builds the real Plan2001 kernel, UEFI loader, MicroPython and host seeder.
2. Builds the normal, nonsmoke BusyBox target ELF.
3. Seeds a new GE/O image containing both native ELFs at separate OIDs.
4. Boots MicroPython in QEMU/OVMF with `--accel kvm:tcg`.
5. Executes `busybox echo ...` as a real child, then `true` and
   `false` as additional children; for each, waits for completion and
   checks cause, state and exit message.
6. Requires the child's actual console output and the parent's final PASS
   marker and successful init.run result in the serial log.

If any step fails, CI fails and publishes diagnostic logs. No existing
GE/O volume or real block device is ever reformatted.

This is still the **first three-applet BusyBox subset**, not a full BusyBox
make-based target build, POSIX shell, broad filesystem support or hardware
validation. A passing QEMU run demonstrates separate process lifecycle,
stdout and completion for these applets, not general Unix compatibility.
