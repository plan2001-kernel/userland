# Boot-time MicroPython code: run ordinary BusyBox children via SPAWN.
# Original native MicroPython owns the boot; the BusyBox ELF's normal main()
# executes inside new processes. No diagnostic C launcher and no kernel edit.

BUSYBOX_OID = 0x4f45584543000005


class Counter:
    def __init__(self, n=0):
        self.n = n


# Current pinned MicroPython persistent graph report requires these names.
shared = Counter(41)
alias = shared


def init():
    shared.n = 0


def busybox(*argv):
    import plan9
    import space

    # Spawned children have no ambient stdout grant. Pass ONLY a writable
    # shared region capability: their libc adapter publishes its bytes.
    # This process maps the same region read-only and forwards verified data
    # using its own, already-authorized MicroPython output.
    region = plan9.region_create(4096)
    view = None
    child = None
    try:
        view = plan9.region_map(region, 0, 1)
        child = space.spawn(BUSYBOX_OID, argv=argv,
                            vhandles=[(region, 256)])
        state = plan9.process_wait(child, 30000)
        if state[3] != plan9.OBJ_PROC_COMPLETED:
            raise RuntimeError("BusyBox child did not complete")
        if state[2] != plan9.OBJ_PROC_CAUSE_EXIT:
            raise RuntimeError("BusyBox child did not exit normally")

        frame = plan9.region_read(view, 4096)
        n = 0
        for i in range(8):
            n |= frame[i] << (8 * i)
        if n > 4088:
            raise RuntimeError("stdout region length out of bounds")
        if n > 0:
            print(frame[8:8+n].decode(), end="")
        return 0 if state[1] == "" else 1
    finally:
        if child is not None:
            plan9.obj_close(child)
        if view is not None:
            plan9.region_unmap(view)
        plan9.obj_close(region)


def external_ls():
    """SPAWN a distinct native ls process with RO path-view input and RW output.

    The view contains only names, not GE/O object authority. The caller owns
    both source and destination regions; the child gets attenuated grants.
    """
    import plan9
    import space

    expected = b"bin\\ndev\\netc\\nhome\\ntmp\\nusr\\nvar\\n"
    source = plan9.region_create(4096)
    stdout = plan9.region_create(4096)
    writer = None
    observer = None
    child = None
    try:
        writer = plan9.region_map(source)
        frame = len(expected).to_bytes(8, "little") + expected
        plan9.region_write(writer, frame)
        # The child gets only read access to this region, no writer right.
        observer = plan9.region_map(stdout, 0, 1)
        child = space.spawn(BUSYBOX_OID, argv=("busybox", "ls", "/"),
                            vhandles=[(source, 1), (stdout, 256)])
        state = plan9.process_wait(child, 30000)
        if state[3] != plan9.OBJ_PROC_COMPLETED:
            raise RuntimeError("external ls child still running")
        if state[2] != plan9.OBJ_PROC_CAUSE_EXIT or state[1] != "":
            raise RuntimeError("external ls process exit failure")
        result = plan9.region_read(observer, 4096)
        n = int.from_bytes(result[0:8], "little")
        if n > 4088 or result[8:8+n] != expected:
            raise RuntimeError("external ls delegated stdout mismatch")
        print("PLAN2001-EXTERNAL-LS-OUTPUT-START")
        print(result[8:8+n].decode(), end="")
        print("PLAN2001-EXTERNAL-LS-PASS")
        return 0
    finally:
        if child is not None:
            plan9.obj_close(child)
        if observer is not None:
            plan9.region_unmap(observer)
        if writer is not None:
            plan9.region_unmap(writer)
        plan9.obj_close(stdout)
        plan9.obj_close(source)


def run():
    assert busybox("busybox", "echo", "PLAN2001-BUSYBOX-SPAWN-ECHO") == 0
    assert busybox("/bin/true") == 0
    assert busybox("busybox", "false") == 1
    assert external_ls() == 0
    print("PLAN2001-BUSYBOX-SPAWN-PASS")
    shared.n += 1
    return 0
