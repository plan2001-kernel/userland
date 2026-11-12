# Boot-time MicroPython code for a REAL child-process BusyBox smoke test.
# This file is compiled to an O-code object by the kernel's pinned mpy-cross.
# It uses the existing space.spawn() and plan9.process_wait() APIs.
# No BusyBox diagnostic launcher or kernel changes are involved.

BUSYBOX_OID = 0x4f45584543000005


class Counter:
    def __init__(self, n=0):
        self.n = n


# The pinned kernel's persistent init report currently requires these names.
shared = Counter(41)
alias = shared


def init():
    shared.n = 0


def busybox(*argv):
    # A process handle is authority to observe completion, not a PID.
    import space
    import plan9

    child = space.spawn(BUSYBOX_OID, argv=argv)
    try:
        state = plan9.process_wait(child, 30000)
        # Tuple is (pid, exit_message, cause, state, generation,
        #           user_ms, sys_ms, real_ms, wait_result, observers).
        if state[3] != plan9.OBJ_PROC_COMPLETED:
            raise RuntimeError("BusyBox child did not complete")
        if state[2] != plan9.OBJ_PROC_CAUSE_EXIT:
            raise RuntimeError("BusyBox child did not exit normally")
        return 0 if state[1] == "" else 1
    finally:
        plan9.obj_close(child)


def run():
    # Actual Plan2001 MicroPython init remains the boot executable.
    # The child uses the standard (non-smoke) BusyBox target ELF main().
    assert busybox("busybox", "echo", "PLAN2001-BUSYBOX-SPAWN-ECHO") == 0
    assert busybox("/bin/true") == 0
    assert busybox("busybox", "false") == 1
    print("PLAN2001-BUSYBOX-SPAWN-PASS")
    shared.n += 1
    return 0
