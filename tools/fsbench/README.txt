FSBENCH v1: Linux ext4 versus Plan2001 GE/O on the same physical machine

Purpose
-------
A small end-to-end Unix file API benchmark, not a crash/recovery test.
Run Linux on the normal /home ext4 partition, then boot Plan2001 on the
same computer and run the preinstalled /home/fsbench from the F5 ash shell.
Do not use QEMU timings for the comparison. Keep the machine otherwise idle,
connected to mains power, and do not change its clock during a run.

Five phases, three rounds by default
-----------------------------------
1. seq_write: create/write/close one 1 MiB file, 64 KiB writes (16 blocks).
2. seq_read_warm: reopen/read/check/close that file eight times (8 MiB).
3. small_create_write_close: 32 new files, one 4 KiB write per file.
4. small_read_warm: reopen/read/check/close each of those files once.
5. small_unlink: remove the 32 small files.

Deterministic pseudorandom data is prepared before timing; every read checks
its content and EOF. No stdio buffering is used for the workload. Console/CSV
output happens after all timed phases. File open/close costs and read content
validation are included. The operations column counts logical data blocks for
sequential phases and complete file operations for small-file phases; it is
not a count of all underlying syscalls. Each invocation owns a fresh run
subdirectory and uses exclusive file creation. Payload files are removed;
the empty run directory is retained to identify the run. No existing file is
truncated. --smoke is only a reduced functional test and cannot be compared.

Completion policies
-------------------
Linux buffered: ordinary POSIX calls, no fsync. This measures page-cache
acceptance for writes, not durable device throughput.
Linux durable: fsync the new empty file and containing directory at creation,
fsync the file after each successful write, and fsync the directory after each
unlink. This waits for the corresponding Linux publication boundaries.
Plan2001 native: use the existing Unix adapter; each write replaces/publishes
a complete framed object, and native namespace mutations commit their changes.
There is no pretend fsync implementation in the program.

Show BOTH Linux policies next to GE/O. Neither is an assertion that the two
filesystems have identical persistence semantics or issue equal flush counts.
This measures the current OS + libc + Unix adapter + filesystem + device stack.
In particular GE/O currently copies complete object values on offset reads
and writes. It is not a pure B-tree benchmark. The partitions share the same
NVMe device but have different layout, free space, caches and filesystem state.
Warm reads intentionally include cache effects; no cache-dropping, direct I/O,
random I/O, large-dataset, wear, power-loss or recovery test is performed.

Linux run
---------
Compile fsbench.c with the Linux-host TinyCC built from the same pinned upstream
revision as the installed Plan2001 compiler (recorded with the baseline), then:

  ./fsbench-linux --dir /home/lapere/fsbench-data --mode buffered > linux-buffered.csv
  ./fsbench-linux --dir /home/lapere/fsbench-data --mode durable > linux-durable.csv

GCC also builds the same source, e.g. gcc -O0 -fno-builtin fsbench.c -o fsbench-linux,
but record that compiler change rather than mixing it into an existing baseline.
For a less order-sensitive comparison, repeat the pair in reverse order too.
The first round is kept visible; compare per-phase medians of the three rounds.
Do not turn buffered versus native results into a raw-device speed claim.

Plan2001 physical HW boot (F5)
-----------------------------
The executable is already installed and admitted. No compilation is required:

  /home/fsbench --dir /home/fsbench-data > /home/fsbench-plan2001.csv
  cat /home/fsbench-plan2001.csv

Success ends in '# PASS verified all file contents'. On any error the command
returns nonzero and prints 'fsbench INVALID'; that run must not be compared.
To rebuild from the installed source with native TinyCC:

  tcc /home/fsbench.c -o /home/fsbench
  tcc --register /home/fsbench

The Linux measurements and machine/build details supplied with this test are:
  /home/fsbench-linux-buffered.csv
  /home/fsbench-linux-durable.csv
  /home/fsbench-linux-environment.txt

After returning to Linux, export /home with otar (Plan2001 must be stopped):

  sudo otar -cf fsbench-home.tar /dev/nvme0n1p5 --root 0x4f45584543000007 -C /home
  tar -xOf fsbench-home.tar fsbench-plan2001.csv > plan2001.csv
  python3 tools/fsbench/compare.py linux-buffered.csv linux-durable.csv plan2001.csv

The comparison script reports medians by phase. It rejects smoke and incomplete
results. Read throughput is MiB/s; small-file metadata performance is best read
as operations/s and elapsed time. Record the booted Plan2001 kernel revision
and storage controller alongside the HW result. A VM smoke test proves that
the program runs; it does not supply the HW performance result.
