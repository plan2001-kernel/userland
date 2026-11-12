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


def run():
    assert busybox("busybox", "echo", "PLAN2001-BUSYBOX-SPAWN-ECHO") == 0
    assert busybox("/bin/true") == 0
    assert busybox("busybox", "false") == 1
    print("PLAN2001-BUSYBOX-SPAWN-PASS")
    shared.n += 1
    return 0
