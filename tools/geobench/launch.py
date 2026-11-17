"""Native MicroPython launcher; only setup/completion, never the measured loop.

run(volume_handle, executable_oid, result_oid, smoke=False)
The caller must already possess the authorized volume Handle. OIDs select
objects within that capability and never authorize opening a volume.
"""
def run(volume_handle, executable_oid, result_oid, smoke=False):
    import plan9 as p
    import space
    result=p.obj_open_ge_oid(volume_handle,result_oid)
    child=None
    try:
        argv=('geobench','smoke') if smoke else ('geobench',)
        child=space.spawn(executable_oid,handle=volume_handle,argv=argv,
                          vhandles=(volume_handle,result))
        return p.process_wait(child,-1)
    finally:
        if child is not None:p.obj_close(child)
        p.obj_close(result)
