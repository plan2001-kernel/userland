#!/usr/bin/env python3
"""Compare only direct whole-object geobench_v1 results, never fsbench/smoke."""
import csv
from pathlib import Path
import statistics
import sys
PHASES={'large_create':(1,1048576),'large_replace_commit':(8,8388608),'large_read_warm':(8,8388608),'small_create_commit':(32,131072),'small_read_warm':(32,131072),'small_replace_commit':(32,131072),'small_replace_batch':(32,131072)}
def load(path):
    text=Path(path).read_text()
    if '# PASS direct whole-object workload; rounds=3;' not in text:raise ValueError('incomplete, smoke, or different-round result')
    rows=list(csv.DictReader(x for x in text.splitlines() if x and not x.startswith('#')))
    if len(rows)!=21:raise ValueError('expected 21 rows')
    values={}
    for phase,(ops,size) in PHASES.items():
        group=[r for r in rows if r['phase']==phase]
        if sorted(int(r['round']) for r in group)!=[1,2,3]:raise ValueError('missing/duplicate rounds')
        if not all(r['geobench_v1']=='geobench_v1' and r['kind']=='benchmark' and int(r['operations'])==ops and int(r['payload_bytes'])==size and int(r['elapsed_ns'])>0 for r in group):raise ValueError('workload mismatch')
        if len({(r['platform'],r['policy'],r['stored_bytes'],r['completion_calls']) for r in group})!=1:raise ValueError('inconsistent rows')
        first=group[0];ns=statistics.median(int(r['elapsed_ns']) for r in group)
        values[phase]=(first,ns)
    return values
if __name__=='__main__':
    if len(sys.argv)<2:raise SystemExit('usage: compare.py RESULTS.csv [RESULTS.csv ...]')
    print('file,platform,policy,phase,median_ms,payload_MiB_per_s,objects_per_s,completion_calls')
    for path in sys.argv[1:]:
        try:values=load(path)
        except (ValueError,KeyError,OSError) as e:raise SystemExit(f'{path}: {e}')
        for phase,(r,ns) in values.items():
            seconds=ns/1e9
            print(f'{Path(path).name},{r["platform"]},{r["policy"]},{phase},{ns/1e6:.3f},{int(r["payload_bytes"])/1048576/seconds:.3f},{int(r["operations"])/seconds:.3f},{r["completion_calls"]}')
