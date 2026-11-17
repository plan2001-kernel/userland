"""Boot REPL initializer. Installer prefixes PROGRAM_OID and RESULT_OID.

Only defines geobench(); boot never runs the timed workload automatically.
"""
def geobench(smoke=False):
    import plan9 as p
    import space
    volume=p.obj_open_volume()
    result=None
    child=None
    try:
        result=p.obj_open_ge_oid(volume,RESULT_OID)
        argv=('geobench','smoke') if smoke else ('geobench',)
        print('GE/O native benchmark running; please wait...')
        child=space.spawn(PROGRAM_OID,handle=volume,argv=argv,
                          vhandles=(volume,result))
        status=p.process_wait(child,-1)
        if status[1]:raise RuntimeError('GEOBENCH INVALID: '+status[1])
        data=p.obj_read(result,16384)
        if len(data)<32 or data[-32:-24]!=b'P2OTAIL1':
            raise ValueError('invalid result footer')
        raw=int.from_bytes(data[-16:-8],'big')
        metadata=int.from_bytes(data[-8:],'big')
        if data[-24:-16]!=b'\x00\x00\x00\x01\x00\x00\x00\x00' or raw+metadata!=len(data)-32:
            raise ValueError('invalid result framing')
        text=data[:raw].decode()
        if not text.startswith('geobench_v1,') or '# PASS direct whole-object workload;' not in text:
            raise ValueError('incomplete benchmark result')
        print(text)
        print('Saved: /home/geobench-plan2001.csv')
    finally:
        if child is not None:p.obj_close(child)
        if result is not None:p.obj_close(result)
        p.obj_close(volume)

print('Native GE/O benchmark ready: geobench()')
