# Direct native GE/O whole-object benchmark

This measures the public native GE/O ABI. The native ELF links only the
workload, native object-call packing, canonical kernel startup and eight
syscall stubs. It has no libc, Newlib, Unix fd table, project namespace,
pathname lookup, POSIX file operations, Python measurement loop or growing-file
adapter. `nm -u geobench.elf` must be empty. MicroPython only delegates the
explicit capabilities, starts the child and observes completion.

Build both adapters with the same GCC optimization level:

```sh
python3 tools/geobench/build.py --output .agent-work/geobench/build
.agent-work/geobench/build/geobench-linux /home/USER/geobench-data > linux.csv
```

Both use exactly the same `core.c`, preallocated buffers and framed object bytes:
raw pseudorandom content, constant CBOR producer metadata and a 32-byte footer.
Payload preparation, framing, integrity comparison, output and close/cleanup
are outside timed operations. Reads use one READ of the whole framed object
through an already-open Handle (Linux: one pread through an already-open fd).
No size query, allocation, partial-file emulation or metadata decoding is done
in the user-space read adapter. Every returned byte is checked after its read's
timer stops. Thus read times are summed individual call times. Write/create
phases measure their complete loop and completion boundary. Native `_NSEC` and
Linux monotonic clocks report nanoseconds; do not adjust the native clock.

Three rounds, seven phases:

| Phase | Workload | Native completion |
|---|---|---|
| large_create | One complete 1 MiB object | CREATE's implicit commit |
| large_replace_commit | Eight changed 1 MiB values, same OID | PUT + COMMIT per value |
| large_read_warm | Eight whole 1 MiB reads | No commit |
| small_create_commit | 32 complete 4 KiB objects | CREATE's implicit commit per object |
| small_read_warm | One whole read of each small object | No commit |
| small_replace_commit | Change all 32 small objects | PUT + COMMIT per object |
| small_replace_batch | Change all 32 small objects again | 32 PUTs, ONE COMMIT |

The native writer uses one explicitly delegated volume Handle for PUT/COMMIT,
which keeps a batch in one native transaction. Readers use the directly
created per-object Handles. Only newly allocated benchmark OIDs are changed;
there is no guessed OID range. A distinct delegated writable GE Handle receives
the CSV after all measurements and validation. Its publication is not timed.
The child cannot mint a new volume capability. There must be exactly one
suitable volume and one writable result-object Handle in its inherited set.

Linux creates one fresh file per object, writes the same complete framed bytes
and fsyncs each new file plus its directory. Replacement writes the whole value,
then fsyncs its file. Batch replacement writes all 32 files and then fsyncs all
32: this is the Linux completion analogue, NOT a cross-file atomic transaction.
The completion_calls column exposes this distinction (native batch: 1 commit,
Linux batch: 32 fsyncs). Results describe the native object API versus Linux ext4
file API on the same hardware, including their kernel/device paths. They are
not pure in-memory GE B-tree timings or raw-device bandwidth. Warm reads retain
cache effects. No crash/recovery or cache-eviction test is performed.

The bounded fixtures are retained: each full run creates three large and 96
small object identities. Closing a Handle is NOT deletion. Existing native
GE/O has no deletion operation exposed here, so the benchmark does not invent
one. Linux also retains its corresponding fixture files in a fresh directory.

## Deploy and run on physical Plan2001

Use otar to install `geobench.elf` and a small placeholder result file such as
`geobench-plan2001.csv` into the selected `/home` project. `otar list` reports
their actual OIDs. Installation paths are not used by the native measurement.
The executable is a native ELF launched from the trusted boot MicroPython
console, not an ash executable requiring the Unix admission catalog.

In a native mpy REPL (for example Win+F2), substitute the installed OIDs:

```python
import plan9 as p, space
v = p.obj_open_volume()
r = p.obj_open_ge_oid(v, RESULT_OID)
h = space.spawn(EXECUTABLE_OID, handle=v, argv=('geobench',), vhandles=(v, r))
p.process_wait(h, -1)
p.obj_close(h)
p.obj_close(r)
p.obj_close(v)
```

`launch.py` packages that setup as `run(v, executable_oid, result_oid)` when
loaded into the REPL. Boot volume acquisition remains subject to the kernel's
existing boot authorization. Do not widen authorization to make the test run.
Add `'smoke'` to the child argv for a functional test only: one round, one
64 KiB object/read/replace and two small objects. The comparator rejects smoke
results. Never use VM timing for the physical hardware comparison.

The native program first publishes an untimed RUNNING marker so that a failed
measurement cannot leave an earlier successful CSV masquerading as a new run.
Always check the child completion status (empty status string means success).
Console output from a spawned native process need not appear in the parent
REPL. To display the persisted result before closing its Handle:

```python
b = p.obj_read(r, 16384)
print(b[:int.from_bytes(b[-16:-8], 'big')].decode())
```

The CSV is published as a framed value into the result OID and is readable
through its existing `/home` binding without a Unix write adapter. After
returning to Linux, export it with otar and compare:

```sh
sudo otar -cf home.tar VOLUME --root ROOT_OID -C /home
tar -xOf home.tar geobench-plan2001.csv > plan2001.csv
python3 tools/geobench/compare.py linux.csv plan2001.csv
```

The comparator accepts only matching full direct-native workloads. Earlier
`fsbench_v1` results describe the Unix adapter and must not be mixed with this
benchmark. Keep compiler/kernel revisions and device/controller information
with the run files, outside tracked project documentation.
